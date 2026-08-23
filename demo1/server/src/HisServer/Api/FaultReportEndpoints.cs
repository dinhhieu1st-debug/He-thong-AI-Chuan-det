using HisServer.Data;
using HisServer.Domain;
using HisServer.Hubs;
using HisServer.Models;
using Microsoft.AspNetCore.SignalR;

namespace HisServer.Api;

/// <summary>
/// The path a broken sensor takes from the bedside to the person who fixes it.
///
/// Before this, that path was a phone call: nothing recorded which bed, which
/// channel, when, or whether anyone picked it up. The split of responsibility
/// is deliberate — the nurse reports a SYMPTOM ("the SpO2 clip reads nothing,
/// I re-seated it twice"), the technician records the DIAGNOSIS. A nurse
/// marking a device "broken" would fill the equipment list with guesses.
/// </summary>
public static class FaultReportEndpoints
{
    public sealed record CreateFaultRequest(string? Channel, string? Note);
    public sealed record ResolveRequest(string? ResolutionNote);

    public static void MapFaultReportEndpoints(this WebApplication app)
    {
        /* Raise a report. Lives under /api/beds because that is what a nurse
         * has in front of them - they do not know, and should not need to
         * know, the hardware address of the box clipped to the bed. */
        app.MapPost("/api/beds/{bedId}/fault-reports", async (
            string bedId,
            CreateFaultRequest request,
            BedStateStore beds,
            DeviceRepository devices,
            FaultReportRepository reports,
            IHubContext<MonitoringHub, IMonitoringClient> hub,
            HttpContext http) =>
        {
            if (beds.Get(bedId) is null)
            {
                return Results.NotFound(new { error = $"Bed '{bedId}' not found" });
            }

            if (!Enum.TryParse<FaultChannel>(request.Channel ?? "Other", ignoreCase: true, out var channel))
            {
                return Results.BadRequest(new { error = "channel must be HR, SPO2, FLOW, DROPS or OTHER" });
            }

            var note = (request.Note ?? string.Empty).Trim();
            if (note.Length > 500) note = note[..500];

            /* The device is looked up here rather than sent by the browser: the
             * nurse reports a bed, and a bed with no device assigned must still
             * be reportable — "there is no working device here" is exactly the
             * kind of thing worth reporting. */
            var device = await devices.GetByBedAsync(bedId);

            var reportedBy = http.User.Identity?.Name ?? "unknown";
            var id = await reports.CreateAsync(bedId, device?.DeviceId, channel, note, reportedBy);

            if (device is not null)
            {
                await devices.AddEventAsync(device.DeviceId, bedId, DeviceEventType.FaultReported,
                                            $"{channel.ToString().ToUpperInvariant()}: {note}");
            }

            var created = await reports.GetAsync(id);
            if (created is not null)
            {
                // Pushed live so the technician sees it appear instead of
                // finding it on their next refresh.
                await hub.Clients.Group(MonitoringHub.FaultGroup).FaultReported(FaultReportDto.From(created));
            }

            return Results.Created($"/api/fault-reports/{id}", new { reportId = id });
        }).RequireAuthorization(Capabilities.ReportFaults);

        /* What the nurse who raised it can see afterwards. Without this they
         * have no way to tell a report that was picked up from one that fell
         * on the floor - and after the first time it falls on the floor, they
         * go back to the telephone. */
        app.MapGet("/api/beds/{bedId}/fault-reports", async (
            string bedId, FaultReportRepository reports) =>
            Results.Ok((await reports.GetForBedAsync(bedId)).Select(FaultReportDto.From)))
            .RequireAuthorization(Capabilities.ReportFaults);

        var group = app.MapGroup("/api/fault-reports")
            .RequireAuthorization(Capabilities.HandleFaults);

        group.MapGet("/", async (FaultReportRepository reports, bool? openOnly) =>
            Results.Ok((await reports.GetQueueAsync(openOnly ?? true)).Select(FaultReportDto.From)));

        group.MapPost("/{reportId:long}/claim", async (
            long reportId, FaultReportRepository reports, HttpContext http,
            IHubContext<MonitoringHub, IMonitoringClient> hub) =>
        {
            var report = await reports.GetAsync(reportId);
            if (report is null) return Results.NotFound();

            await reports.UpdateStatusAsync(reportId, FaultStatus.InProgress,
                                            http.User.Identity?.Name ?? "unknown", null);

            var updated = await reports.GetAsync(reportId);
            if (updated is not null) await hub.Clients.Group(MonitoringHub.FaultGroup).FaultReported(FaultReportDto.From(updated));
            return Results.Ok(new { ok = true });
        });

        group.MapPost("/{reportId:long}/resolve", async (
            long reportId, ResolveRequest request, FaultReportRepository reports,
            DeviceRepository devices, HttpContext http,
            IHubContext<MonitoringHub, IMonitoringClient> hub) =>
        {
            var report = await reports.GetAsync(reportId);
            if (report is null) return Results.NotFound();

            var handledBy = http.User.Identity?.Name ?? "unknown";
            var note = (request.ResolutionNote ?? string.Empty).Trim();
            await reports.UpdateStatusAsync(reportId, FaultStatus.Resolved, handledBy,
                                            note.Length == 0 ? null : note);

            if (report.DeviceId is not null)
            {
                await devices.AddEventAsync(report.DeviceId, report.BedId,
                                            DeviceEventType.FaultResolved,
                                            note.Length == 0 ? null : note);
            }

            var updated = await reports.GetAsync(reportId);
            if (updated is not null) await hub.Clients.Group(MonitoringHub.FaultGroup).FaultReported(FaultReportDto.From(updated));
            return Results.Ok(new { ok = true });
        });
    }
}
