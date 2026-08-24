using HisServer.Data;
using HisServer.Domain;
using HisServer.Hubs;
using HisServer.Models;
using Microsoft.AspNetCore.SignalR;
using System.Text.Json;

namespace HisServer.Api;

public static class BedEndpoints
{
    public static void MapBedEndpoints(this IEndpointRouteBuilder app)
    {
        /* No group-wide policy on purpose. This group mixes three audiences:
         * the ward view (nurses), the bed DIRECTORY (technicians assigning a
         * device, administrators building the ward), and bed administration.
         * A single group policy would either lock out two of them or hand the
         * other two more than they need. */
        var group = app.MapGroup("/api/beds");

        group.MapGet("/", (BedStateStore store) =>
            Results.Ok(store.GetAll().OrderBy(b => b.BedId).Select(BedDto.From)))
            .RequireAuthorization(Capabilities.ViewWard);

        /* The bed DIRECTORY: which beds exist, in which room, with which
         * device, and whether someone is in them. No vitals, no patient name.
         *
         * This is what a technician sees when choosing a bed for a device they
         * are assigning, and what an administrator sees when laying out the
         * ward. Both need bed numbers; neither needs a heart rate, so this is
         * a separate endpoint rather than a filtered view of the one above -
         * a filter is something you can forget to apply. */
        group.MapGet("/directory", async (BedStateStore store, DeviceRepository devices) =>
        {
            /* Which device serves a bed is owned by the devices table
             * (assigned_bed_id), set when a technician assigns it. The beds
             * table has its own device_id column left over from the seed data
             * that nothing maintains - reading that instead is why this page
             * first showed "no device assigned" for a bed whose device was
             * plainly online. One direction of the link, one source of truth. */
            /* Only devices that have actually announced themselves count as
             * "the device for this bed". The demo seed leaves a placeholder
             * per bed with no address; showing one here would tell an
             * administrator a bed is equipped when nothing is plugged in. */
            var byBed = (await devices.GetAllAsync())
                .Where(d => !string.IsNullOrWhiteSpace(d.AssignedBedId)
                            && !string.IsNullOrWhiteSpace(d.Eui64))
                .GroupBy(d => d.AssignedBedId!, StringComparer.OrdinalIgnoreCase)
                .ToDictionary(g => g.Key, g => g.First().DeviceId, StringComparer.OrdinalIgnoreCase);

            return Results.Ok(store.GetAll()
                .OrderBy(b => b.BedId, StringComparer.OrdinalIgnoreCase)
                .Select(b => new
                {
                    bedId = b.BedId,
                    room = b.Room,
                    hasPatient = !string.IsNullOrWhiteSpace(b.PatientName),
                    deviceId = byBed.GetValueOrDefault(b.BedId)
                }));
        }).RequireAuthorization(Capabilities.ViewBedDirectory);

        group.MapGet("/{bedId}", (string bedId, BedStateStore store) =>
        {
            var bed = store.Get(bedId);
            return bed is null ? Results.NotFound() : Results.Ok(BedDto.From(bed));
        }).RequireAuthorization(Capabilities.ViewWard);

        /* Charted history for one bed, used by the per-metric time-series
         * plots in the bed detail view.
         *
         * `minutes` picks the window (default 60, capped at a week so a typo
         * in the query string can't ask the DB for everything ever recorded).
         * `limit` caps how many points come back: samples land every
         * VitalsSave.IntervalSeconds (10s), so an hour is ~360 points, but a
         * multi-day window would be tens of thousands - far more than a few
         * hundred pixels of chart can show, and pointlessly heavy to ship to
         * the browser. The newest points are the ones kept. */
        group.MapGet("/{bedId}/history", async (
            string bedId,
            BedStateStore store,
            VitalSampleRepository sampleRepository,
            int? minutes,
            int? limit,
            CancellationToken cancellationToken) =>
        {
            if (store.Get(bedId) is null)
            {
                return Results.NotFound(new { error = $"Bed '{bedId}' not found" });
            }

            var windowMinutes = Math.Clamp(minutes ?? 60, 1, 60 * 24 * 7);
            var maxPoints = Math.Clamp(limit ?? 1000, 10, 5000);

            var to = DateTime.UtcNow;
            var from = to.AddMinutes(-windowMinutes);

            var samples = await sampleRepository.GetHistoryAsync(
                bedId, from, to, maxPoints, cancellationToken);

            return Results.Ok(new
            {
                bedId,
                from,
                to,
                windowMinutes,
                samples
            });
        }).RequireAuthorization(Capabilities.ViewWard);

        group.MapPost("/", async (
            CreateBedRequest request,
            BedStateStore store,
            BedRepository repository,
            IHubContext<MonitoringHub, IMonitoringClient> hub) =>
        {
            if (string.IsNullOrWhiteSpace(request.BedId))
            {
                return Results.BadRequest(new { error = "bedId is required" });
            }

            if (store.Get(request.BedId) is not null)
            {
                return Results.Conflict(new { error = $"Bed '{request.BedId}' already exists" });
            }

            var bed = store.Upsert(request.BedId, state =>
            {
                state.Room = request.Room ?? string.Empty;
                state.Status = BedStatus.Offline;
            });

            await repository.UpsertAsync(bed);
            await hub.Clients.Group(MonitoringHub.WardGroup).BedUpdated(BedDto.From(bed));
            await NotifyDirectoryAsync(hub);
            return Results.Created($"/api/beds/{bed.BedId}", BedDto.From(bed));
        }).RequireAuthorization(Capabilities.ManageBeds);

        /* Deleting a bed.
         *
         * Refused outright in two cases rather than warned about, because
         * neither is something a confirmation dialog should be able to talk
         * somebody through: a bed with a patient admitted to it, and a bed a
         * device is still reporting to. The first would drop a patient from the
         * ward view while they are still in the bed; the second would leave a
         * device delivering readings to a bed that no longer exists.
         *
         * Removed from BOTH places, which is the part that is easy to get
         * wrong: the database row AND the in-memory store the ward view is
         * served from. Deleting only the row leaves the bed on screen until
         * the next restart - measured during testing, and the reason this
         * comment exists. */
        group.MapDelete("/{bedId}", async (
            string bedId,
            BedStateStore store,
            BedRepository repository,
            DeviceRepository devices,
            IHubContext<MonitoringHub, IMonitoringClient> hub) =>
        {
            var bed = store.Get(bedId);
            if (bed is null)
            {
                return Results.NotFound(new { error = $"Bed '{bedId}' does not exist" });
            }

            if (!string.IsNullOrWhiteSpace(bed.PatientName))
            {
                return Results.Conflict(new
                {
                    error = $"Bed '{bedId}' still has a patient admitted ({bed.PatientName}). "
                          + "Discharge the patient before removing the bed."
                });
            }

            var attached = (await devices.GetAllAsync())
                .FirstOrDefault(d => string.Equals(d.AssignedBedId, bedId,
                                                   StringComparison.OrdinalIgnoreCase));
            if (attached is not null)
            {
                return Results.Conflict(new
                {
                    error = $"Device {attached.DeviceId} is still assigned to bed '{bedId}'. "
                          + "Reassign or remove the device first (Devices tab)."
                });
            }

            await repository.DeleteAsync(bedId);
            store.Remove(bedId);

            await hub.Clients.Group(MonitoringHub.WardGroup).BedRemoved(bedId);
            await NotifyDirectoryAsync(hub);
            return Results.NoContent();
        }).RequireAuthorization(Capabilities.ManageBeds);

        group.MapPut("/{bedId}", async (
            string bedId,
            UpdateBedRequest request,
            BedStateStore store,
            BedRepository repository,
            DeviceRepository devices,
            IHubContext<MonitoringHub, IMonitoringClient> hub) =>
        {
            if (store.Get(bedId) is null)
            {
                return Results.NotFound();
            }

            var bed = store.Upsert(bedId, state =>
            {
                if (request.Room is not null)
                {
                    state.Room = request.Room;
                }

                if (request.DeviceId is not null)
                {
                    state.DeviceId = request.DeviceId;
                }
            });

            await repository.UpsertAsync(bed);
            await hub.Clients.Group(MonitoringHub.WardGroup).BedUpdated(BedDto.From(bed));

            /* A device carries its own copy of the room, taken when it was
             * assigned to a bed and never refreshed. Renaming the bed's room
             * therefore left the Devices tab showing the old name forever. The
             * bed owns the room; the device copy follows it.
             *
             * Looked up through the DEVICES table, which is the maintained
             * direction of the bed-device link. BedState.DeviceId is only
             * written when a technician assigns a device through the UI, so for
             * a seeded bed it is null and this sync would quietly never run. */
            if (request.Room is not null)
            {
                var device = await devices.GetByBedAsync(bedId);
                if (device is not null && !string.Equals(device.Room, bed.Room, StringComparison.Ordinal))
                {
                    device.Room = bed.Room;
                    await devices.UpsertAsync(device);
                    await hub.Clients.Group(MonitoringHub.DeviceGroup).DeviceUpdated(DeviceDto.From(device));
                }
            }

            await NotifyDirectoryAsync(hub);
            return Results.Ok(BedDto.From(bed));
        }).RequireAuthorization(Capabilities.ManageBeds);

        group.MapPut("/{bedId}/target-flow", async (
            string bedId,
            SetTargetFlowRequest request,
            BedStateStore store,
            BedConnectionRegistry connectionRegistry,
            DeviceRepository devices) =>
        {
            if (store.Get(bedId) is not { } flowBed)
            {
                return Results.NotFound(new { error = $"Bed '{bedId}' not found" });
            }

            if (request.TargetFlowMlH <= 0)
            {
                return Results.BadRequest(new { error = "targetFlowMlH must be a positive number" });
            }

            var commandLine = await AddressToBedDeviceAsync(devices, flowBed,
                $$"""{"cmd":"set_target_flow_ml_h","value":{{request.TargetFlowMlH}}}""");
            return await SendDeviceCommandAsync(bedId, commandLine, connectionRegistry,
                new { bedId, targetFlowMlH = request.TargetFlowMlH });
        }).RequireAuthorization(Capabilities.ControlBed);

        group.MapPut("/{bedId}/target-drops", async (
            string bedId,
            SetTargetDropsRequest request,
            BedStateStore store,
            BedConnectionRegistry connectionRegistry,
            BedRepository repository,
            DeviceRepository devices,
            IHubContext<MonitoringHub, IMonitoringClient> hub) =>
        {
            if (store.Get(bedId) is not { } dropsBed)
            {
                return Results.NotFound(new { error = $"Bed '{bedId}' not found" });
            }

            if (request.TargetDropsPerMin is < 1 or > 240)
            {
                return Results.BadRequest(new { error = "targetDropsPerMin must be between 1 and 240 dpm" });
            }

            var commandLine = await AddressToBedDeviceAsync(devices, dropsBed,
                $$"""{"cmd":"set_target_drops_per_min","value":{{request.TargetDropsPerMin}}}""");
            var delivered = await connectionRegistry.TrySendCommandAsync(bedId, commandLine);
            if (!delivered)
            {
                return Results.Conflict(new
                {
                    error = $"Bed '{bedId}' has no live gateway connection right now - " +
                            "the drop target was not changed."
                });
            }

            // The firmware immediately closes only the alarm gate and starts a
            // fresh 20-interval drip window. Mirror that accepted state now so
            // the old warning does not linger on the page during the round trip.
            var bed = store.Upsert(bedId, state =>
            {
                state.TargetDropsPerMin = request.TargetDropsPerMin;
                state.DropTrainingSamples = 0;
                state.AlertsArmed = false;
                state.Status = BedStatus.Stable;
                state.Hysteresis = VitalsStatusEvaluator.MetricHysteresis.None;
                state.AlertMessage = null;
                state.FinalAlertLevel = null;
                state.AlertLevel = 0;
                state.LineBranch = false;
                state.PatientBranch = false;
                state.DripAnomaly = false;
            });
            await repository.UpsertAsync(bed);
            await hub.Clients.Group(MonitoringHub.WardGroup).BedUpdated(BedDto.From(bed));

            return Results.Accepted(value: new
            {
                bedId,
                targetDropsPerMin = request.TargetDropsPerMin,
                dropTrainingSamples = 0,
                alertsArmed = false
            });
        }).RequireAuthorization(Capabilities.ControlBed);

        /* Bắt đầu / tạm dừng theo dõi.
         *
         * Đây là nút y tá bấm sau khi đã treo bình, kẹp cảm biến và tare cân
         * xong. Trước khi bấm, thiết bị vẫn đọc và vẫn hiện số nhưng KHÔNG chạy
         * AI và KHÔNG báo động - vì mọi con số trong lúc lắp đặt đều là rác, và
         * một khoa quen với báo giả là một khoa sẽ bỏ qua tiếng còi thật.
         *
         * Trạng thái nằm ở THIẾT BỊ chứ không phải ở server: nếu giữ trên server
         * thì mất mạng là chiếc máy đầu giường lại tự báo động trong lúc chưa ai
         * bảo nó bắt đầu, và đó đúng là tình huống nó phải im nhất. */
        group.MapPost("/{bedId}/monitoring", async (
            string bedId,
            MonitoringRequest request,
            BedStateStore store,
            BedConnectionRegistry connectionRegistry,
            BedRepository repository,
            DeviceRepository devices,
            IHubContext<MonitoringHub, IMonitoringClient> hub) =>
        {
            if (store.Get(bedId) is not { } monitoringBed)
            {
                return Results.NotFound(new { error = $"Bed '{bedId}' not found" });
            }

            var value = request.Enabled ? 1 : 0;
            var delivered = await connectionRegistry.TrySendCommandAsync(bedId,
                await AddressToBedDeviceAsync(devices, monitoringBed,
                    $$"""{"cmd":"set_monitoring","value":{{value}}}"""));
            if (!delivered)
            {
                return Results.Conflict(new
                {
                    error = $"Bed '{bedId}' has no live gateway connection right now - " +
                            "monitoring was not changed. Reconnect the gateway and try again."
                });
            }

            // Reflect the accepted command immediately instead of leaving a
            // red/yellow banner visible until the next Zigbee report arrives.
            // The next device reading remains authoritative and confirms the
            // monitoring bit end-to-end.
            var bed = store.Upsert(bedId, state =>
            {
                state.Monitoring = request.Enabled;
                state.AlertsArmed = request.Enabled ? false : null;
                state.DropTrainingSamples = request.Enabled ? 0 : null;
                state.VitalsTrainingSamples = request.Enabled ? 0 : null;
                // Starting enters a silent training phase; pausing is also
                // silent. In both cases clear any alarm left from the previous
                // monitoring session immediately.
                state.Status = BedStatus.Stable;
                state.Hysteresis = VitalsStatusEvaluator.MetricHysteresis.None;
                state.AlertMessage = null;
                state.AlertLevel = 0;
                // Protocol capability is learned from telemetry. Do not invent
                // a canonical level here or an older device would be treated
                // as if it supported the new on-chip severity contract.
                state.FinalAlertLevel = null;
                state.LineBranch = false;
                state.PatientBranch = false;
                state.DripAnomaly = false;
                state.VitalsAnomaly = false;
                state.AeAlarm = false;
                if (!request.Enabled)
                {
                    state.DropTrainingSamples = null;
                    state.VitalsTrainingSamples = null;
                }
            });
            await repository.UpsertAsync(bed);
            await hub.Clients.Group(MonitoringHub.WardGroup).BedUpdated(BedDto.From(bed));

            return Results.Accepted(value: new
            {
                bedId,
                monitoring = request.Enabled,
                alertsArmed = bed.AlertsArmed,
                dropTrainingSamples = bed.DropTrainingSamples,
                vitalsTrainingSamples = bed.VitalsTrainingSamples,
                status = bed.Status.ToString()
            });
        }).RequireAuthorization(Capabilities.ControlBed);

        group.MapPost("/{bedId}/tare", async (
            string bedId,
            BedStateStore store,
            BedConnectionRegistry connectionRegistry,
            DeviceRepository devices) =>
        {
            if (store.Get(bedId) is not { } tareBed)
            {
                return Results.NotFound(new { error = $"Bed '{bedId}' not found" });
            }

            return await SendDeviceCommandAsync(bedId,
                await AddressToBedDeviceAsync(devices, tareBed, """{"cmd":"reset_tare"}"""),
                connectionRegistry, new { bedId, action = "reset_tare" });
        }).RequireAuthorization(Capabilities.ControlBed);

        group.MapPost("/{bedId}/recalibrate-hr", async (
            string bedId,
            BedStateStore store,
            BedConnectionRegistry connectionRegistry,
            DeviceRepository devices) =>
        {
            if (store.Get(bedId) is not { } hrBed)
            {
                return Results.NotFound(new { error = $"Bed '{bedId}' not found" });
            }

            return await SendDeviceCommandAsync(bedId,
                await AddressToBedDeviceAsync(devices, hrBed,
                    """{"cmd":"recalibrate_hr_baseline"}"""),
                connectionRegistry, new { bedId, action = "recalibrate_hr_baseline" });
        }).RequireAuthorization(Capabilities.ControlBed);

        /* Vitals test mode: feed the AI branch a fabricated heart rate and SpO2
         * so the alarm path can be exercised without an unwell patient.
         *
         * The fabricated values go ONLY into the AI branch. The real sensor
         * readings keep flowing and keep being reported separately, which is
         * why the console can show "Real" and "AI test" side by side - and why
         * switching back to real data does not cost the device its learned
         * history.
         *
         * That pairing is also the only honest confirmation that a command
         * crossed the whole chain. A toast means the SERVER accepted the
         * request; the AI-input figures changing means the CHIP did.
         *
         * 0, 2 and 3 only. 1 is missing because the firmware's fake level enum
         * has no "force normal" - real data already is level 1 - and the
         * gateway rejects anything else outright. */
        group.MapPut("/{bedId}/vitals-test-mode", async (
            string bedId,
            SetVitalsTestModeRequest request,
            BedStateStore store,
            BedConnectionRegistry connectionRegistry,
            DeviceRepository devices) =>
        {
            if (store.Get(bedId) is not { } testBed)
            {
                return Results.NotFound(new { error = $"Bed '{bedId}' not found" });
            }

            if (request.Mode is not (0 or 2 or 3))
            {
                return Results.BadRequest(new
                {
                    error = "mode must be 0 (real sensor data), 2 (force attention) or 3 (force alarm)"
                });
            }

            var commandLine = await AddressToBedDeviceAsync(devices, testBed,
                $$"""{"cmd":"set_vitals_test_mode","value":{{request.Mode}}}""");

            return await SendDeviceCommandAsync(bedId, commandLine, connectionRegistry,
                new { bedId, vitalsTestMode = request.Mode });
        }).RequireAuthorization(Capabilities.ControlBed);
    }

    /// <summary>
    /// Delivers a command over the SAME TCP connection the gateway uses to
    /// forward vitals (see BedConnectionRegistry) - the gateway republishes
    /// it to zigbee2mqtt's "&lt;topic&gt;/set", which writes the matching
    /// Zigbee attribute down to the device (app.c's
    /// sl_zigbee_af_post_attribute_change_cb() picks it up from there).
    /// </summary>
    /// <summary>
    /// Tells every open bed directory to re-fetch itself.
    ///
    /// Separate from BedUpdated because administrators and technicians hold
    /// ViewBedDirectory but not ViewWard, so they are not in WardGroup and
    /// never see BedUpdated at all.
    /// </summary>
    /// <summary>
    /// Names the device a bed command is meant for, so the gateway does not
    /// have to guess.
    ///
    /// The gateway honours a "deviceId" field and otherwise falls back to
    /// whichever device published to it most recently (publish_command() in
    /// server_client.c). That fallback is harmless for a gateway carrying one
    /// bed and dangerous for a gateway carrying several: a target-rate change
    /// for one bed gets published to whichever device happened to report last,
    /// which is a different patient's pump. Every bed command therefore states
    /// its device explicitly, as the OTA commands already did.
    ///
    /// The device is resolved from the DEVICES table (assigned_bed_id), which
    /// is the one direction of the bed-device link that is actually maintained.
    /// BedState.DeviceId is only written when a technician assigns a device
    /// through the UI, so it is null for any bed that came from the seed or was
    /// created by its first reading - addressing from it left the id off the
    /// command entirely and put the gateway straight back on its guess.
    /// </summary>
    private static async Task<string> AddressToBedDeviceAsync(
        DeviceRepository devices, BedState bed, string commandLine)
    {
        var deviceId = (await devices.GetByBedAsync(bed.BedId))?.DeviceId;
        if (string.IsNullOrWhiteSpace(deviceId))
        {
            deviceId = bed.DeviceId;
        }

        if (string.IsNullOrWhiteSpace(deviceId))
        {
            // Nothing to address it to. The command still fails the registry
            // check a moment later, which is the honest outcome.
            return commandLine;
        }

        // Escaped rather than interpolated raw: a device id comes off the
        // Zigbee network, and a stray quote would produce a malformed line the
        // gateway silently drops.
        var escaped = JsonEncodedText.Encode(deviceId).ToString();
        return commandLine[..^1] + $$""","deviceId":"{{escaped}}"}""";
    }

    private static Task NotifyDirectoryAsync(IHubContext<MonitoringHub, IMonitoringClient> hub) =>
        hub.Clients.Group(MonitoringHub.DirectoryGroup).BedDirectoryChanged();

    private static async Task<IResult> SendDeviceCommandAsync(
        string bedId, string commandLine, BedConnectionRegistry connectionRegistry, object acceptedValue)
    {
        var delivered = await connectionRegistry.TrySendCommandAsync(bedId, commandLine);

        if (!delivered)
        {
            return Results.Conflict(new
            {
                error = $"Bed '{bedId}' has no live gateway connection right now - " +
                        "the command could not be delivered. It will need to be resent " +
                        "once the bed's gateway reconnects."
            });
        }

        return Results.Accepted(value: acceptedValue);
    }
}

public sealed record CreateBedRequest(string BedId, string? Room);

/// <summary>Bật (true) hoặc tạm dừng (false) việc theo dõi ở một giường.</summary>
public sealed record MonitoringRequest(bool Enabled);

public sealed record UpdateBedRequest(string? Room, string? DeviceId);

public sealed record SetTargetFlowRequest(int TargetFlowMlH);

public sealed record SetTargetDropsRequest(int TargetDropsPerMin);

/// <summary>0 real sensor data, 2 force the attention band, 3 force the alarm band.</summary>
public sealed record SetVitalsTestModeRequest(int Mode);
