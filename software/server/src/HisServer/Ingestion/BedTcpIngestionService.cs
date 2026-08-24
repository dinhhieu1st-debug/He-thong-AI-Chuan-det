using System.Net;
using System.Net.Sockets;
using System.Text;
using System.Text.Json;
using HisServer.Data;
using HisServer.Domain;
using HisServer.Hubs;
using HisServer.Models;
using HisServer.Services;
using Microsoft.AspNetCore.SignalR;
using Microsoft.Extensions.Options;

namespace HisServer.Ingestion;

public sealed class TcpOptions
{
    public int Port { get; set; } = 5000;
}

/// <summary>
/// Listens for bed vitals over a raw TCP socket — newline-delimited JSON, one
/// connection per device/gateway. Replaces the old WinForms app's
/// BedDataReceiver.cs; the wire protocol is unchanged (same alias-tolerant
/// field names in BedDataParser) since device firmware can't be modified.
/// </summary>
public sealed class BedTcpIngestionService : BackgroundService
{
    private readonly BedStateStore bedStateStore;
    private readonly BedRepository bedRepository;
    private readonly VitalSampleRepository vitalSampleRepository;
    private readonly AlertRepository alertRepository;
    private readonly VitalsPersistenceCoordinator persistenceCoordinator;
    private readonly FcmPushService fcmPushService;
    private readonly BedConnectionRegistry connectionRegistry;
    private readonly DeviceRepository deviceRepository;
    private readonly OtaStatusRegistry otaRegistry;
    private readonly IHubContext<MonitoringHub, IMonitoringClient> hub;
    private readonly IOptionsMonitor<TcpOptions> options;
    private readonly IOptionsMonitor<OfflineOptions> offlineOptions;
    private readonly IOptionsMonitor<DeviceHealthOptions> deviceHealthOptions;
    private readonly ILogger<BedTcpIngestionService> logger;

    public BedTcpIngestionService(
        BedStateStore bedStateStore,
        BedRepository bedRepository,
        VitalSampleRepository vitalSampleRepository,
        AlertRepository alertRepository,
        VitalsPersistenceCoordinator persistenceCoordinator,
        FcmPushService fcmPushService,
        BedConnectionRegistry connectionRegistry,
        DeviceRepository deviceRepository,
        OtaStatusRegistry otaRegistry,
        IHubContext<MonitoringHub, IMonitoringClient> hub,
        IOptionsMonitor<TcpOptions> options,
        IOptionsMonitor<OfflineOptions> offlineOptions,
        IOptionsMonitor<DeviceHealthOptions> deviceHealthOptions,
        ILogger<BedTcpIngestionService> logger)
    {
        this.bedStateStore = bedStateStore;
        this.bedRepository = bedRepository;
        this.vitalSampleRepository = vitalSampleRepository;
        this.alertRepository = alertRepository;
        this.persistenceCoordinator = persistenceCoordinator;
        this.fcmPushService = fcmPushService;
        this.connectionRegistry = connectionRegistry;
        this.deviceRepository = deviceRepository;
        this.otaRegistry = otaRegistry;
        this.hub = hub;
        this.options = options;
        this.offlineOptions = offlineOptions;
        this.deviceHealthOptions = deviceHealthOptions;
        this.logger = logger;
    }

    protected override async Task ExecuteAsync(CancellationToken stoppingToken)
    {
        var port = options.CurrentValue.Port;
        var listener = new TcpListener(IPAddress.Any, port);
        listener.Start();
        logger.LogInformation("Bed vitals TCP ingestion listening on port {Port}.", port);

        try
        {
            while (!stoppingToken.IsCancellationRequested)
            {
                TcpClient client;
                try
                {
                    client = await listener.AcceptTcpClientAsync(stoppingToken);
                }
                catch (OperationCanceledException)
                {
                    break;
                }

                _ = HandleClientAsync(client, stoppingToken);
            }
        }
        finally
        {
            listener.Stop();
        }
    }

    /// <summary>
    /// Processes one gateway vitals payload received through the HTTPS bridge.
    /// The parsing, device routing, persistence, alerts and SignalR updates are
    /// deliberately identical to the raw TCP path.
    /// </summary>
    public async Task<string?> IngestReadingLineAsync(string line, CancellationToken cancellationToken = default)
    {
        if (string.IsNullOrWhiteSpace(line))
        {
            throw new ArgumentException("The gateway payload is empty.", nameof(line));
        }

        if (TryHandleControlLine(line, out var handled) && handled)
        {
            return null;
        }

        var reading = await RouteToAssignedBedAsync(
            BedDataParser.Parse(line), cancellationToken);

        await ProcessReadingAsync(reading, cancellationToken);
        await UpdateDeviceHealthAsync(reading, cancellationToken);
        return reading.BedId;
    }

    private async Task HandleClientAsync(TcpClient client, CancellationToken cancellationToken)
    {
        /* Every bed this connection is registered for in BedConnectionRegistry,
         * so a command from the REST API - a nurse's target-rate change, a tare
         * - can be written back down the same socket. Registered lazily,
         * because a bed id is not known until the first line parses.
         *
         * A SET, not a single id. One Pi gateway forwards every device paired
         * to it, so one socket routinely carries several beds. While this was a
         * single slot, each reading from a different bed unregistered the
         * previous one, leaving only the most recently reporting bed
         * addressable: every other bed's command failed with "no live gateway
         * connection" while that gateway was connected the whole time, and
         * which bed worked changed from second to second. */
        var registeredBedIds = new HashSet<string>(StringComparer.OrdinalIgnoreCase);

        try
        {
            using (client)
            await using (var stream = client.GetStream())
            using (var reader = new StreamReader(stream, Encoding.UTF8))
            {
                while (!cancellationToken.IsCancellationRequested)
                {
                    string? line;
                    try
                    {
                        line = await reader.ReadLineAsync(cancellationToken);
                    }
                    catch (IOException)
                    {
                        return;
                    }

                    if (line is null)
                    {
                        return;
                    }

                    if (string.IsNullOrWhiteSpace(line))
                    {
                        continue;
                    }

                    try
                    {
                        /* A gateway sends two kinds of line on this socket.
                         * Vitals have no "type" field (the format predates
                         * this), so anything carrying one is a control message
                         * and is handled separately - which keeps every older
                         * gateway working untouched. */
                        if (TryHandleControlLine(line, out var handled) && handled)
                        {
                            continue;
                        }

                        var reading = await RouteToAssignedBedAsync(
                            BedDataParser.Parse(line), cancellationToken);

                        if (registeredBedIds.Add(reading.BedId))
                        {
                            connectionRegistry.Register(reading.BedId, stream);
                        }

                        await ProcessReadingAsync(reading, cancellationToken);
                        await UpdateDeviceHealthAsync(reading, cancellationToken);
                    }
                    catch (Exception ex)
                    {
                        logger.LogWarning(ex, "Invalid bed data line received: {Line}", line);
                    }
                }
            }
        }
        finally
        {
            foreach (var bedId in registeredBedIds)
            {
                connectionRegistry.Unregister(bedId);
            }
        }
    }

    /// <summary>
    /// Handles a non-vitals line. Returns true when the line WAS a control
    /// message (so the caller must not try to parse it as a reading).
    ///
    /// Today there is one: a gateway telling the server that a Zigbee device
    /// joined the network. Before this, a device that joined was invisible to
    /// the server - the technician had to type its address into the Devices
    /// tab by hand, from a zigbee2mqtt log they had to SSH in to read, and a
    /// typo produced a device row matching nothing.
    /// </summary>
    private bool TryHandleControlLine(string line, out bool handled)
    {
        handled = false;
        try
        {
            using var document = JsonDocument.Parse(line);
            if (!document.RootElement.TryGetProperty("type", out var typeElement))
            {
                return false;
            }

            var type = typeElement.GetString();

            if (string.Equals(type, "ota_status", StringComparison.OrdinalIgnoreCase))
            {
                var otaRoot = document.RootElement;
                var otaDeviceId = ReadString(otaRoot, "deviceId") ?? ReadString(otaRoot, "device_id");
                if (string.IsNullOrWhiteSpace(otaDeviceId))
                {
                    return false;
                }

                /* The gateway sends -1 for "not known", never 0 - see the note
                 * on OtaStatus.Progress. Translate that back to null here so
                 * nothing downstream has to remember the convention. */
                var progress = ReadInt(otaRoot, "progress");
                var remaining = ReadInt(otaRoot, "remainingSeconds");

                var installed = ReadInt(otaRoot, "installedVersion");
                var latest = ReadInt(otaRoot, "latestVersion");

                var status = otaRegistry.Update(
                    otaDeviceId!,
                    ReadString(otaRoot, "state"),
                    progress is >= 0 ? progress : null,
                    remaining is >= 0 ? remaining : null,
                    ReadString(otaRoot, "message"),
                    out var previousState,
                    installed is >= 0 ? installed : null,
                    latest is >= 0 ? latest : null);

                /* History, only on a real transition. The device republishes
                 * its OTA state roughly once a second; writing a row for each
                 * would bury the handful of events a technician cares about
                 * under thousands that say "still idle". */
                if (status.State != previousState)
                {
                    _ = Task.Run(() => RecordOtaHistoryAsync(otaDeviceId!, status, previousState));
                }

                _ = Task.Run(() => hub.Clients.Group(MonitoringHub.DeviceGroup)
                                      .OtaStatusChanged(OtaStatusDto.From(status)));
                handled = true;
                return true;
            }

            if (!string.Equals(type, "device_announce", StringComparison.OrdinalIgnoreCase))
            {
                return false;
            }

            var root = document.RootElement;
            var deviceId = ReadString(root, "deviceId") ?? ReadString(root, "device_id");
            if (string.IsNullOrWhiteSpace(deviceId))
            {
                return false;
            }

            var friendlyName = ReadString(root, "friendlyName") ?? ReadString(root, "friendly_name");
            _ = Task.Run(() => AnnounceDeviceAsync(deviceId!, friendlyName));
            handled = true;
            return true;
        }
        catch (JsonException)
        {
            // Not JSON at all - let the reading parser produce the real error.
            return false;
        }
    }

    private static string? ReadString(JsonElement root, string name) =>
        root.TryGetProperty(name, out var element) && element.ValueKind == JsonValueKind.String
            ? element.GetString()
            : null;

    /// <summary>Reads an integer property, or null when absent or not a number.</summary>
    private static int? ReadInt(System.Text.Json.JsonElement root, string name) =>
        root.TryGetProperty(name, out var el)
        && el.ValueKind == System.Text.Json.JsonValueKind.Number
        && el.TryGetInt32(out var v) ? v : null;

    /// <summary>
    /// Writes one line of firmware history.
    ///
    /// Only three states are worth a row. "Available" and "up to date" are the
    /// answer to a question somebody asked, not something that happened to the
    /// device, and a log where most rows are answers to questions is a log
    /// nobody reads to the end.
    /// </summary>
    private async Task RecordOtaHistoryAsync(string deviceId, OtaStatus status,
                                             OtaState previousState)
    {
        var (installed, latest) = otaRegistry.VersionsFor(deviceId);

        var (type, detail) = status.State switch
        {
            OtaState.Starting => (DeviceEventType.FirmwareUpdateStarted,
                latest is not null
                    ? $"Update started: v{installed?.ToString() ?? "?"} to v{latest}"
                    : "Firmware update started"),

            /* The versions here are read BEFORE the device reboots and reports
             * its new one, so "installed" is still the old image - which is
             * exactly the number the history needs: what it came from. */
            OtaState.Done => (DeviceEventType.FirmwareUpdated,
                latest is not null
                    ? $"Updated: v{installed?.ToString() ?? "?"} to v{latest}"
                    : "Firmware updated"),

            OtaState.Failed => (DeviceEventType.FirmwareUpdateFailed,
                string.IsNullOrWhiteSpace(status.Message)
                    ? "Update failed"
                    : $"Update failed: {status.Message}"),

            _ => (default(DeviceEventType), (string?)null),
        };

        if (detail is null) return;

        /* A failure that follows nothing is noise: zigbee2mqtt reports "failed"
         * for a CHECK that timed out as well as for a transfer that broke, and
         * only the second is firmware history. */
        if (type == DeviceEventType.FirmwareUpdateFailed
            && previousState is not (OtaState.Starting or OtaState.Updating))
        {
            return;
        }

        try
        {
            var record = await deviceRepository.GetAsync(deviceId);
            await deviceRepository.AddEventAsync(deviceId, record?.AssignedBedId, type,
                                                 Truncate(detail, 255));
        }
        catch (Exception ex)
        {
            logger.LogWarning(ex, "Could not record firmware history for {Device}", deviceId);
        }
    }

    private static string Truncate(string text, int max) =>
        text.Length <= max ? text : text[..max];

    private async Task AnnounceDeviceAsync(string deviceId, string? friendlyName)
    {
        try
        {
            var existing = await deviceRepository.GetAsync(deviceId);

            /* An already-assigned device that rejoins (a power cut, a firmware
             * flash) must NOT be pushed back to PENDING and lose its bed - it
             * is the same device coming back, and the technician should not
             * have to re-assign it every time the ward loses power. */
            var device = existing ?? new DeviceRecord
            {
                DeviceId = deviceId,
                DeviceType = DeviceType.Xg26,
                Status = DeviceStatus.Pending
            };

            device.Eui64 = deviceId;
            device.LastSeenAt = DateTime.UtcNow;

            await deviceRepository.UpsertAsync(device);

            /* DeviceDiscovered is what makes the Devices tab announce "New
             * device joined" out loud, so it must fire only for a device the
             * server has genuinely never seen.
             *
             * It used to fire on every announce. A gateway re-announces after
             * every reconnect, and a gateway carrying more than one device
             * re-announced constantly, so a technician got a stream of "new
             * device" toasts for hardware that had been assigned for days -
             * which is exactly how a real new device ends up unnoticed.
             *
             * A device that is merely coming back still needs its row
             * refreshed, and that is DeviceUpdated: same payload, repaints
             * silently, no toast. */
            if (existing is null)
            {
                logger.LogInformation("New Zigbee device announced: {DeviceId} ({FriendlyName})",
                    deviceId, friendlyName ?? "unnamed");
                await hub.Clients.Group(MonitoringHub.DeviceGroup).DeviceDiscovered(DeviceDto.From(device));
            }
            else
            {
                await hub.Clients.Group(MonitoringHub.DeviceGroup).DeviceUpdated(DeviceDto.From(device));
            }
        }
        catch (Exception ex)
        {
            logger.LogWarning(ex, "Could not record announced device {DeviceId}", deviceId);
        }
    }

    /* Which bed each device was last routed to, so a change of assignment is
     * noticed once rather than looked up on every reading. */
    private readonly Dictionary<string, string> deviceBedRoute =
        new(StringComparer.OrdinalIgnoreCase);

    /// <summary>
    /// Sends a reading to the bed its DEVICE is assigned to.
    ///
    /// The gateway puts a bed id on every reading, but that comes from its
    /// command line - one fixed bed baked into a systemd unit on the Pi. So
    /// moving a device to another bed in the console changed the equipment
    /// record and nothing else: the readings kept arriving at the old bed, and
    /// a nurse looking at BED-301 saw an empty bed while BED-101 showed
    /// somebody else's patient. Assignment has to decide where data lands, or
    /// it is not an assignment at all.
    ///
    /// The gateway's own bed id stays as the fallback for a device that is not
    /// assigned yet, and for older gateways that send no device id - neither
    /// should stop data arriving.
    /// </summary>
    private async Task<BedReading> RouteToAssignedBedAsync(BedReading reading,
                                                          CancellationToken cancellationToken)
    {
        if (string.IsNullOrWhiteSpace(reading.DeviceId))
        {
            return reading;
        }

        DeviceRecord? device;
        try
        {
            device = await deviceRepository.GetAsync(reading.DeviceId, cancellationToken);
        }
        catch (Exception ex)
        {
            logger.LogWarning(ex, "Could not resolve device {DeviceId}; using the bed the gateway sent",
                reading.DeviceId);
            return reading;
        }

        if (device?.AssignedBedId is not { Length: > 0 } assignedBed)
        {
            return reading;
        }

        if (!string.Equals(assignedBed, reading.BedId, StringComparison.OrdinalIgnoreCase))
        {
            if (!deviceBedRoute.TryGetValue(reading.DeviceId, out var previous)
                || !string.Equals(previous, assignedBed, StringComparison.OrdinalIgnoreCase))
            {
                logger.LogInformation(
                    "Device {DeviceId} is assigned to {AssignedBed}; routing its readings there " +
                    "instead of {GatewayBed}", reading.DeviceId, assignedBed, reading.BedId);
                deviceBedRoute[reading.DeviceId] = assignedBed;
            }

            var room = string.IsNullOrWhiteSpace(device.Room) ? reading.Room : device.Room;
            return reading with { BedId = assignedBed, Room = room };
        }

        deviceBedRoute[reading.DeviceId] = assignedBed;
        return reading;
    }

    /* When the currently-lost set of channels first went quiet, per bed. A
     * single reading cannot tell a five-minute outage from a one-second one,
     * so the clock lives here rather than in the evaluator. Cleared as soon as
     * every channel reports again. */
    private readonly Dictionary<string, (string Channels, DateTime Since)> channelLossSince =
        new(StringComparer.OrdinalIgnoreCase);

    /// <summary>
    /// Attaches live health to the device assigned to this bed.
    ///
    /// Runs on every reading - once a second per bed - so it writes to the
    /// database only when something a technician would notice actually changed.
    /// Writing each packet would be 86,400 identical rows a day per bed, and
    /// would bury the handful of state changes that matter.
    /// </summary>
    private async Task UpdateDeviceHealthAsync(BedReading reading, CancellationToken cancellationToken)
    {
        DeviceRecord? device;
        try
        {
            /* Prefer the device that actually sent this reading. Looking it up
             * by bed was a guess that went wrong once already, when a deleted
             * device let a placeholder row collect live health. */
            device = !string.IsNullOrWhiteSpace(reading.DeviceId)
                ? await deviceRepository.GetAsync(reading.DeviceId, cancellationToken)
                : await deviceRepository.GetByBedAsync(reading.BedId, cancellationToken);
        }
        catch (Exception ex)
        {
            logger.LogWarning(ex, "Could not load the device for bed {BedId}", reading.BedId);
            return;
        }

        if (device is null)
        {
            return;   // no device assigned to this bed yet - nothing to update
        }

        var now = DateTime.UtcNow;
        var lost = DeviceHealthEvaluator.LostChannels(reading);

        if (lost is null)
        {
            channelLossSince.Remove(reading.BedId);
        }
        else if (!channelLossSince.TryGetValue(reading.BedId, out var tracked) || tracked.Channels != lost)
        {
            // A DIFFERENT set of channels is now quiet - restart the clock, so
            // "SpO2 lost, then Flow lost too" does not inherit SpO2's age.
            channelLossSince[reading.BedId] = (lost, now);
        }

        var lostSince = channelLossSince.TryGetValue(reading.BedId, out var entry)
            ? entry.Since : (DateTime?)null;

        var status = DeviceHealthEvaluator.Evaluate(reading, lostSince, now, deviceHealthOptions.CurrentValue);

        var statusChanged = device.Status != status;
        var channelsChanged = device.ChannelsLost != lost;

        // Link quality wanders by a few points constantly; only a real move is
        // worth a write.
        var linkChanged = reading.LinkQuality is not null
                          && (device.LinkQuality is null
                              || Math.Abs(device.LinkQuality.Value - reading.LinkQuality.Value) >= 10);

        // Even with nothing else changed, refresh "last data" occasionally so
        // the technician can see the device is alive without waiting for it to
        // break.
        var staleTimestamp = device.LastDataAt is null
                             || (now - device.LastDataAt.Value).TotalSeconds >= 30;

        if (!statusChanged && !channelsChanged && !linkChanged && !staleTimestamp)
        {
            return;
        }

        try
        {
            await deviceRepository.UpdateHealthAsync(
                device.DeviceId, status,
                reading.LinkQuality ?? device.LinkQuality,
                lost, now, cancellationToken);

            if (statusChanged)
            {
                var type = status switch
                {
                    DeviceStatus.SensorFault => DeviceEventType.SensorFault,
                    DeviceStatus.Offline => DeviceEventType.Offline,
                    _ => DeviceEventType.Online
                };
                var detail = status == DeviceStatus.SensorFault
                    ? $"No signal from: {lost}"
                    : null;
                await deviceRepository.AddEventAsync(device.DeviceId, device.AssignedBedId,
                                                     type, detail, cancellationToken);
            }

            device.Status = status;
            device.ChannelsLost = lost;
            device.LinkQuality = reading.LinkQuality ?? device.LinkQuality;
            device.LastDataAt = now;
            device.LastSeenAt = now;
            await hub.Clients.Group(MonitoringHub.DeviceGroup).DeviceUpdated(DeviceDto.From(device));
        }
        catch (Exception ex)
        {
            logger.LogWarning(ex, "Could not update health for device {DeviceId}", device.DeviceId);
        }
    }

    private async Task ProcessReadingAsync(BedReading reading, CancellationToken cancellationToken)
    {
        var previousBed = bedStateStore.Get(reading.BedId);
        /* Zigbee attribute reports are partial: an HR report normally contains
         * no startup counters and no AlertsArmed attribute. Null therefore
         * means "not present in this frame", not "training ended". Preserve
         * the last authoritative values (including the immediate state set by
         * the Start endpoint) until the device explicitly reports replacements.
         * Without this merge COLLECTING DATA appeared for one frame and then
         * vanished as soon as the next HR-only report arrived. */
        if (previousBed is not null)
        {
            reading = reading with
            {
                DropTrainingSamples = reading.DropTrainingSamples
                                      ?? previousBed.DropTrainingSamples,
                VitalsTrainingSamples = reading.VitalsTrainingSamples
                                        ?? previousBed.VitalsTrainingSamples,
                AlertsArmed = reading.AlertsArmed ?? previousBed.AlertsArmed,
                FinalAlertLevel = reading.FinalAlertLevel ?? previousBed.FinalAlertLevel
            };
        }
        var previousStatus = previousBed?.Status ?? BedStatus.Offline;
        var previousHysteresis = previousBed?.Hysteresis ?? VitalsStatusEvaluator.MetricHysteresis.None;
        var status = VitalsStatusEvaluator.Evaluate(reading, previousHysteresis);
        var nextHysteresis = VitalsStatusEvaluator.ComputeNextHysteresis(reading, previousHysteresis);

        string? alertMessage = null;
        string alertType = string.Empty;
        if (status is BedStatus.Warning or BedStatus.Critical)
        {
            (alertType, alertMessage) = VitalsStatusEvaluator.DescribeAlert(reading, previousHysteresis);
        }

        var bed = bedStateStore.Upsert(reading.BedId, state =>
        {
            /* A bed's room is DIRECTORY data. An administrator sets it in the
             * Bed directory, and that is the only writer allowed to win.
             *
             * The gateway also puts a room on every reading, but that value
             * comes from BED_ROOM in a config file on the Pi and ships as the
             * literal string "Unknown room". Assigning it here overwrote the
             * administrator's rename on the very next reading - about a second
             * later - so a room change appeared to save and then silently
             * reverted on the nurse's screen. Only fill a room in when the bed
             * does not have one yet, which is the bed-created-by-a-reading
             * case this line was originally for. */
            if (string.IsNullOrWhiteSpace(state.Room))
            {
                state.Room = reading.Room;
            }

            state.Status = status;
            state.Hysteresis = nextHysteresis;
            state.Spo2 = reading.Spo2;
            state.HeartRate = reading.HeartRate;
            state.DripRate = reading.DripRate;
            state.FlowRate = reading.FlowRate;
            state.HeartRateSignal = reading.HeartRateSignal;
            state.Spo2Signal = reading.Spo2Signal;
            state.FlowSignal = reading.FlowSignal;
            state.DripRateSignal = reading.DripRateSignal;
            state.LineBlocked = reading.LineBlocked;
            state.Monitoring = reading.Monitoring;
            state.DropTrainingSamples = reading.DropTrainingSamples;
            state.VitalsTrainingSamples = reading.VitalsTrainingSamples;
            state.AlertsArmed = reading.AlertsArmed;
            state.AeAlarm = reading.AeAlarm;
            state.WeightG = reading.WeightG;
            state.DropsPerMin = reading.DropsPerMin;
            state.TargetFlowMlH = reading.TargetFlowMlH;
            state.TargetDropsPerMin = reading.TargetDropsPerMin;
            state.TareInProgress = reading.TareInProgress;
            state.TareJustCompleted = reading.TareJustCompleted;
            state.HrBaselineJustCompleted = reading.HrBaselineJustCompleted;
            state.HrBaselineSecondsRemaining = reading.HrBaselineSecondsRemaining;
            state.HrBaselineBpm = reading.HrBaselineBpm;

            // The firmware has no wall-clock time (only relative uptime), so
            // THIS server stamps the actual timestamp the moment it detects a
            // completion - lets the UI show "captured/tared at HH:MM:SS"
            // persistently rather than relying on a transient toast the
            // doctor might not be looking at.
            //
            // We use the PERSISTENT event counters (TareEventCount /
            // HrBaselineEventCount) rather than the one-shot *_just_completed
            // flags above: those flags pulse true for exactly one firmware
            // report and can be missed entirely if that report happens to
            // fire before Zigbee reporting was configured (e.g. the very
            // fast auto-tare at boot can race with network join, while the
            // slower 60s HR window essentially never does - this exact
            // asymmetry was observed in practice). A persistent count can't
            // be missed: any difference from what we saw last time - however
            // late we happen to observe it - means a completion happened.
            if (reading.TareEventCount is int newTareCount
                && newTareCount != state.LastSeenTareEventCount)
            {
                state.LastTareCompletedAt = reading.ReceivedAt;
                state.LastSeenTareEventCount = newTareCount;
            }
            if (reading.HrBaselineEventCount is int newHrBaselineCount
                && newHrBaselineCount != state.LastSeenHrBaselineEventCount)
            {
                state.HrBaselineCapturedAt = reading.ReceivedAt;
                state.LastSeenHrBaselineEventCount = newHrBaselineCount;
            }

            state.TsReady = reading.TsReady;
            state.TsAnomaly = reading.TsAnomaly;
            state.TsEarlyWarning = reading.TsEarlyWarning;
            state.TsTrend = reading.TsTrend;
            state.HrForecast16s = reading.HrForecast16s;
            state.Spo2Forecast16s = reading.Spo2Forecast16s;
            state.HrTrendBpmPerMin = reading.HrTrendBpmPerMin;
            state.TsAnomalyScoreX100 = reading.TsAnomalyScoreX100;
            state.DropsTrend = reading.DropsTrend;
            state.DropsTrendDpmPerMin = reading.DropsTrendDpmPerMin;
            state.DropsForecast16s = reading.DropsForecast16s;
            state.HrForecastTrusted = reading.HrForecastTrusted;
            state.DropsForecastTrusted = reading.DropsForecastTrusted;

            state.AlertLevel = reading.AlertLevel;
            state.FinalAlertLevel = reading.FinalAlertLevel;
            state.LineBranch = reading.LineBranch;
            state.PatientBranch = reading.PatientBranch;
            state.DripAnomaly = reading.DripAnomaly;
            state.VitalsAnomaly = reading.VitalsAnomaly;
            state.LineState = reading.LineState;
            state.RemainingMl = reading.RemainingMl;
            state.RemainingMin = reading.RemainingMin;

            /* The chip's echo of what it is running on. Kept separate from
             * the measured HeartRate/Spo2 above so a console can show both and
             * a nurse can see that a command took effect. */
            state.AiInputHeartRate = reading.AiInputHeartRate;
            state.AiInputSpo2 = reading.AiInputSpo2;
            state.VitalsLevel = reading.VitalsLevel;
            state.ServerDropLevel = reading.ServerDropLevel;
            state.VitalsTestMode = reading.VitalsTestMode;
            state.Spo2Low = reading.Spo2Low;
            state.HeartRateAbnormal = reading.HeartRateAbnormal;
            state.DropIntervalMs = reading.DropIntervalMs;

            state.AlertMessage = alertMessage;
            state.LastDataAt = reading.ReceivedAt;
        });

        await hub.Clients.Group(MonitoringHub.WardGroup).BedUpdated(BedDto.From(bed));

        // The `beds` row is the durable "current state" record (used for restart
        // recovery), so it is always kept fresh — only the `vital_samples` history
        // insert is throttled, to avoid writing a time-series row on every tick.
        await bedRepository.UpsertAsync(bed, cancellationToken);

        if (persistenceCoordinator.ShouldSave(reading.BedId, reading.ReceivedAt))
        {
            await vitalSampleRepository.SaveAsync(reading, bed.DeviceId, status, cancellationToken);
        }

        if (AlertTransitionTracker.ShouldRaiseAlert(previousStatus, status))
        {
            var alertId = await alertRepository.InsertAsync(new AlertRecord
            {
                BedId = reading.BedId,
                /* The BED's room, not the reading's. The reading carries
                 * whatever BED_ROOM says on the Pi - "Unknown room" by
                 * default - so alert history used to record that instead of
                 * the room the administrator actually named. An alert is read
                 * back long after the fact, by someone trying to work out
                 * where it happened. */
                Room = bed.Room,
                DeviceId = bed.DeviceId,
                Level = status,
                AlertType = alertType,
                Message = alertMessage ?? string.Empty,
                Spo2 = reading.Spo2,
                HeartRate = reading.HeartRate,
                DripRate = reading.DripRate,
                CreatedAt = reading.ReceivedAt
            }, cancellationToken);

            var alert = await alertRepository.GetAsync(alertId, cancellationToken);
            if (alert is not null)
            {
                await hub.Clients.Group(MonitoringHub.WardGroup).AlertCreated(AlertDto.From(alert));
                await fcmPushService.SendAlertAsync(alert, cancellationToken);
            }
        }
    }
}
