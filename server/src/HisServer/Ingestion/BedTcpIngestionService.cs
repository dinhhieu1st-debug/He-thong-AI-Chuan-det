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

    private async Task HandleClientAsync(TcpClient client, CancellationToken cancellationToken)
    {
        // Tracks which bedId this connection is currently registered under in
        // BedConnectionRegistry, so a command sent from the REST API (e.g. a
        // doctor's target flow rate change) can be written back down this
        // same socket. Registered lazily on the first successfully-parsed
        // line (bedId isn't known before that), and always unregistered when
        // the connection ends - a gateway only ever forwards one bed, so this
        // never actually changes mid-connection in practice.
        string? registeredBedId = null;

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
                        if (TryHandleControlLine(line, stream, out var handled) && handled)
                        {
                            continue;
                        }

                        var reading = BedDataParser.Parse(line);

                        if (registeredBedId != reading.BedId)
                        {
                            if (registeredBedId is not null)
                            {
                                connectionRegistry.Unregister(registeredBedId);
                            }

                            connectionRegistry.Register(reading.BedId, stream);
                            registeredBedId = reading.BedId;
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
            if (registeredBedId is not null)
            {
                connectionRegistry.Unregister(registeredBedId);
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
    private bool TryHandleControlLine(string line, NetworkStream stream, out bool handled)
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
            if (existing is null)
            {
                logger.LogInformation("New Zigbee device announced: {DeviceId} ({FriendlyName})",
                    deviceId, friendlyName ?? "unnamed");
            }

            await deviceRepository.UpsertAsync(device);
            await hub.Clients.Group(MonitoringHub.DeviceGroup).DeviceDiscovered(DeviceDto.From(device));
        }
        catch (Exception ex)
        {
            logger.LogWarning(ex, "Could not record announced device {DeviceId}", deviceId);
        }
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
            device = await deviceRepository.GetByBedAsync(reading.BedId, cancellationToken);
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
        var status = VitalsStatusEvaluator.Evaluate(reading);
        var previousStatus = bedStateStore.Get(reading.BedId)?.Status ?? BedStatus.Offline;

        string? alertMessage = null;
        string alertType = string.Empty;
        if (status is BedStatus.Warning or BedStatus.Critical)
        {
            (alertType, alertMessage) = VitalsStatusEvaluator.DescribeAlert(reading);
        }

        var bed = bedStateStore.Upsert(reading.BedId, state =>
        {
            state.Room = reading.Room;
            state.Status = status;
            state.Spo2 = reading.Spo2;
            state.HeartRate = reading.HeartRate;
            state.Temperature = reading.Temperature;
            state.DripRate = reading.DripRate;
            state.FlowRate = reading.FlowRate;
            state.HeartRateSignal = reading.HeartRateSignal;
            state.Spo2Signal = reading.Spo2Signal;
            state.FlowSignal = reading.FlowSignal;
            state.DripRateSignal = reading.DripRateSignal;
            state.LineBlocked = reading.LineBlocked;
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
                Room = reading.Room,
                DeviceId = bed.DeviceId,
                Level = status,
                AlertType = alertType,
                Message = alertMessage ?? string.Empty,
                Spo2 = reading.Spo2,
                HeartRate = reading.HeartRate,
                Temperature = reading.Temperature,
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
