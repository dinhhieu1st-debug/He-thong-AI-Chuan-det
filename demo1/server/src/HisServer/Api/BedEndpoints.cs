using HisServer.Data;
using HisServer.Domain;
using HisServer.Hubs;
using HisServer.Models;
using Microsoft.AspNetCore.SignalR;

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
            return Results.NoContent();
        }).RequireAuthorization(Capabilities.ManageBeds);

        group.MapPut("/{bedId}", async (
            string bedId,
            UpdateBedRequest request,
            BedStateStore store,
            BedRepository repository,
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
            return Results.Ok(BedDto.From(bed));
        }).RequireAuthorization(Capabilities.ManageBeds);

        group.MapPut("/{bedId}/target-flow", async (
            string bedId,
            SetTargetFlowRequest request,
            BedStateStore store,
            BedConnectionRegistry connectionRegistry) =>
        {
            if (store.Get(bedId) is null)
            {
                return Results.NotFound(new { error = $"Bed '{bedId}' not found" });
            }

            if (request.TargetFlowMlH <= 0)
            {
                return Results.BadRequest(new { error = "targetFlowMlH must be a positive number" });
            }

            var commandLine = $$"""{"cmd":"set_target_flow_ml_h","value":{{request.TargetFlowMlH}}}""";
            return await SendDeviceCommandAsync(bedId, commandLine, connectionRegistry,
                new { bedId, targetFlowMlH = request.TargetFlowMlH });
        }).RequireAuthorization(Capabilities.ControlBed);

        group.MapPut("/{bedId}/target-drops", async (
            string bedId,
            SetTargetDropsRequest request,
            BedStateStore store,
            BedConnectionRegistry connectionRegistry) =>
        {
            if (store.Get(bedId) is null)
            {
                return Results.NotFound(new { error = $"Bed '{bedId}' not found" });
            }

            if (request.TargetDropsPerMin <= 0)
            {
                return Results.BadRequest(new { error = "targetDropsPerMin must be a positive number" });
            }

            var commandLine = $$"""{"cmd":"set_target_drops_per_min","value":{{request.TargetDropsPerMin}}}""";
            return await SendDeviceCommandAsync(bedId, commandLine, connectionRegistry,
                new { bedId, targetDropsPerMin = request.TargetDropsPerMin });
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
            IHubContext<MonitoringHub, IMonitoringClient> hub) =>
        {
            if (store.Get(bedId) is null)
            {
                return Results.NotFound(new { error = $"Bed '{bedId}' not found" });
            }

            var value = request.Enabled ? 1 : 0;
            var delivered = await connectionRegistry.TrySendCommandAsync(bedId,
                $$"""{"cmd":"set_monitoring","value":{{value}}}""");
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
                if (!request.Enabled)
                {
                    state.Status = BedStatus.Stable;
                    state.Hysteresis = VitalsStatusEvaluator.MetricHysteresis.None;
                    state.AlertMessage = null;
                    state.AlertLevel = 1;
                    state.AlertsArmed = false;
                    state.LineBranch = false;
                    state.PatientBranch = false;
                    state.DripAnomaly = false;
                    state.VitalsAnomaly = false;
                    state.AeAlarm = false;
                }
            });
            await repository.UpsertAsync(bed);
            await hub.Clients.Group(MonitoringHub.WardGroup).BedUpdated(BedDto.From(bed));

            return Results.Accepted(value: new
            {
                bedId,
                monitoring = request.Enabled,
                status = bed.Status.ToString()
            });
        }).RequireAuthorization(Capabilities.ControlBed);

        group.MapPost("/{bedId}/tare", async (
            string bedId,
            BedStateStore store,
            BedConnectionRegistry connectionRegistry) =>
        {
            if (store.Get(bedId) is null)
            {
                return Results.NotFound(new { error = $"Bed '{bedId}' not found" });
            }

            return await SendDeviceCommandAsync(bedId, """{"cmd":"reset_tare"}""", connectionRegistry,
                new { bedId, action = "reset_tare" });
        }).RequireAuthorization(Capabilities.ControlBed);

        group.MapPost("/{bedId}/recalibrate-hr", async (
            string bedId,
            BedStateStore store,
            BedConnectionRegistry connectionRegistry) =>
        {
            if (store.Get(bedId) is null)
            {
                return Results.NotFound(new { error = $"Bed '{bedId}' not found" });
            }

            return await SendDeviceCommandAsync(bedId, """{"cmd":"recalibrate_hr_baseline"}""", connectionRegistry,
                new { bedId, action = "recalibrate_hr_baseline" });
        }).RequireAuthorization(Capabilities.ControlBed);

        group.MapPost("/{bedId}/vitals-test", async (
            string bedId,
            VitalsTestRequest request,
            BedStateStore store,
            BedConnectionRegistry connectionRegistry) =>
        {
            if (store.Get(bedId) is null)
                return Results.NotFound(new { error = $"Bed '{bedId}' not found" });
            if (request.Level is not (0 or 2 or 3))
                return Results.BadRequest(new { error = "level must be 0, 2 or 3" });

            return await SendDeviceCommandAsync(
                bedId,
                $$"""{"cmd":"set_vitals_test_mode","value":{{request.Level}}}""",
                connectionRegistry,
                new { bedId, vitalsTestMode = request.Level });
        }).RequireAuthorization(Capabilities.ControlBed);
    }

    /// <summary>
    /// Delivers a command over the SAME TCP connection the gateway uses to
    /// forward vitals (see BedConnectionRegistry) - the gateway republishes
    /// it to zigbee2mqtt's "&lt;topic&gt;/set", which writes the matching
    /// Zigbee attribute down to the device (app.c's
    /// sl_zigbee_af_post_attribute_change_cb() picks it up from there).
    /// </summary>
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
public sealed record VitalsTestRequest(int Level);
