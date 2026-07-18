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
        var group = app.MapGroup("/api/beds");

        group.MapGet("/", (BedStateStore store) =>
            Results.Ok(store.GetAll().OrderBy(b => b.BedId).Select(BedDto.From)));

        group.MapGet("/{bedId}", (string bedId, BedStateStore store) =>
        {
            var bed = store.Get(bedId);
            return bed is null ? Results.NotFound() : Results.Ok(BedDto.From(bed));
        });

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
            await hub.Clients.All.BedUpdated(BedDto.From(bed));
            return Results.Created($"/api/beds/{bed.BedId}", BedDto.From(bed));
        });

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
            await hub.Clients.All.BedUpdated(BedDto.From(bed));
            return Results.Ok(BedDto.From(bed));
        });
    }
}

public sealed record CreateBedRequest(string BedId, string? Room);

public sealed record UpdateBedRequest(string? Room, string? DeviceId);
