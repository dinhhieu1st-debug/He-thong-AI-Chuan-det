using HisServer.Data;
using HisServer.Domain;
using HisServer.Hubs;
using HisServer.Models;
using Microsoft.AspNetCore.SignalR;

namespace HisServer.Api;

public static class AlertEndpoints
{
    public static void MapAlertEndpoints(this IEndpointRouteBuilder app)
    {
        var group = app.MapGroup("/api/alerts")
            .RequireAuthorization(Capabilities.ViewWard);

        group.MapGet("/", async (
            AlertRepository repository,
            bool? ack,
            string? level,
            int? page,
            int? pageSize) =>
        {
            BedStatus? parsedLevel = null;
            if (!string.IsNullOrWhiteSpace(level) && Enum.TryParse<BedStatus>(level, ignoreCase: true, out var lvl))
            {
                parsedLevel = lvl;
            }

            var query = new AlertQuery
            {
                Acknowledged = ack,
                Level = parsedLevel,
                Page = page is null or <= 0 ? 1 : page.Value,
                PageSize = pageSize is null or <= 0 ? 80 : pageSize.Value
            };

            var (items, totalCount) = await repository.QueryAsync(query);
            return Results.Ok(new
            {
                items = items.Select(AlertDto.From),
                totalCount,
                page = query.Page,
                pageSize = query.PageSize
            });
        });

        group.MapGet("/{id:long}", async (long id, AlertRepository repository) =>
        {
            var alert = await repository.GetAsync(id);
            return alert is null ? Results.NotFound() : Results.Ok(AlertDto.From(alert));
        });

        group.MapPost("/{id:long}/ack", async (
            long id,
            AlertRepository repository,
            IHubContext<MonitoringHub, IMonitoringClient> hub,
            HttpContext http) =>
        {
            var alert = await repository.GetAsync(id);
            if (alert is null)
            {
                return Results.NotFound();
            }

            // Taken from the signed-in session, never from the request body:
            // a client-supplied name would make the audit trail worthless.
            var acknowledgedAt = await repository.AcknowledgeAsync(id, http.User.Identity?.Name);
            await hub.Clients.All.AlertAcknowledged(id, acknowledgedAt);
            return Results.Ok(new { ok = true, acknowledgedAt });
        }).RequireAuthorization(Capabilities.AckAlerts);
    }
}
