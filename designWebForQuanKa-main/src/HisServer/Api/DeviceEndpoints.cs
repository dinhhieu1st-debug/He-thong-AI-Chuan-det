using HisServer.Data;
using HisServer.Hubs;
using HisServer.Models;
using Microsoft.AspNetCore.SignalR;

namespace HisServer.Api;

public static class DeviceEndpoints
{
    public static void MapDeviceEndpoints(this IEndpointRouteBuilder app)
    {
        var group = app.MapGroup("/api/devices");

        group.MapGet("/", async (DeviceRepository repository) =>
            Results.Ok((await repository.GetAllAsync()).Select(DeviceDto.From)));

        group.MapGet("/{deviceId}", async (string deviceId, DeviceRepository repository) =>
        {
            var device = await repository.GetAsync(deviceId);
            return device is null ? Results.NotFound() : Results.Ok(DeviceDto.From(device));
        });

        group.MapPost("/", async (
            UpsertDeviceRequest request,
            DeviceRepository repository,
            IHubContext<MonitoringHub, IMonitoringClient> hub) =>
        {
            if (string.IsNullOrWhiteSpace(request.DeviceId))
            {
                return Results.BadRequest(new { error = "deviceId is required" });
            }

            if (await repository.GetAsync(request.DeviceId) is not null)
            {
                return Results.Conflict(new { error = $"Device '{request.DeviceId}' already exists" });
            }

            var device = ToRecord(request);
            await repository.UpsertAsync(device);
            await hub.Clients.All.DeviceUpdated(DeviceDto.From(device));
            return Results.Created($"/api/devices/{device.DeviceId}", DeviceDto.From(device));
        });

        group.MapPut("/{deviceId}", async (
            string deviceId,
            UpsertDeviceRequest request,
            DeviceRepository repository,
            IHubContext<MonitoringHub, IMonitoringClient> hub) =>
        {
            if (await repository.GetAsync(deviceId) is null)
            {
                return Results.NotFound();
            }

            var device = ToRecord(request with { DeviceId = deviceId });
            await repository.UpsertAsync(device);
            await hub.Clients.All.DeviceUpdated(DeviceDto.From(device));
            return Results.Ok(DeviceDto.From(device));
        });

        group.MapDelete("/{deviceId}", async (string deviceId, DeviceRepository repository) =>
        {
            var deleted = await repository.DeleteAsync(deviceId);
            return deleted ? Results.NoContent() : Results.NotFound();
        });
    }

    private static DeviceRecord ToRecord(UpsertDeviceRequest request) => new()
    {
        DeviceId = request.DeviceId,
        DeviceType = Enum.TryParse<DeviceType>(request.DeviceType, ignoreCase: true, out var type) ? type : DeviceType.Xg26,
        AssignedBedId = request.AssignedBedId,
        Room = request.Room,
        Status = Enum.TryParse<DeviceStatus>(request.Status, ignoreCase: true, out var status) ? status : DeviceStatus.Pending,
        BatteryPercent = request.BatteryPercent,
        Rssi = request.Rssi,
        Eui64 = request.Eui64,
        LastSeenAt = request.LastSeenAt
    };
}

public sealed record UpsertDeviceRequest(
    string DeviceId,
    string DeviceType,
    string? AssignedBedId,
    string? Room,
    string Status,
    int? BatteryPercent,
    int? Rssi,
    string? Eui64,
    DateTime? LastSeenAt);
