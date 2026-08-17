using HisServer.Data;
using HisServer.Domain;
using HisServer.Hubs;
using HisServer.Models;
using Microsoft.AspNetCore.SignalR;

namespace HisServer.Api;

public static class DeviceEndpoints
{
    public static void MapDeviceEndpoints(this IEndpointRouteBuilder app)
    {
        // Everything here is equipment work, so one policy covers the group.
        var group = app.MapGroup("/api/devices")
            .RequireAuthorization(Capabilities.ManageDevices);

        group.MapGet("/", async (DeviceRepository repository) =>
            Results.Ok((await repository.GetAllAsync()).Select(DeviceDto.From)))
            .RequireAuthorization(Capabilities.ManageDevices);

        group.MapGet("/{deviceId}", async (string deviceId, DeviceRepository repository) =>
        {
            var device = await repository.GetAsync(deviceId);
            return device is null ? Results.NotFound() : Results.Ok(DeviceDto.From(device));
        }).RequireAuthorization(Capabilities.ManageDevices);

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
            await hub.Clients.Group(MonitoringHub.DeviceGroup).DeviceUpdated(DeviceDto.From(device));
            return Results.Created($"/api/devices/{device.DeviceId}", DeviceDto.From(device));
        }).RequireAuthorization(Capabilities.ManageDevices);

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
            await hub.Clients.Group(MonitoringHub.DeviceGroup).DeviceUpdated(DeviceDto.From(device));
            return Results.Ok(DeviceDto.From(device));
        }).RequireAuthorization(Capabilities.ManageDevices);

        /* The technician's log for one device: joined, went offline, a channel
         * died, a fault was reported. Deliberately NOT the System Log, which
         * carries SpO2 and heart rate in every row. */
        group.MapGet("/{deviceId}/events", async (string deviceId, DeviceRepository repository) =>
            Results.Ok((await repository.GetEventsAsync(deviceId)).Select(DeviceEventDto.From)));

        /* Ask the gateways to re-read their device inventory.
         *
         * zigbee2mqtt republishes its device list only when the Zigbee network
         * changes, so deleting a record here changes nothing on the radio and
         * the device would not be seen again until the gateway restarted. This
         * is what makes "remove it, then add it back" a thing a technician can
         * actually do - and test - from this page. */
        group.MapPost("/rescan", async (BedConnectionRegistry connections) =>
        {
            var delivered = await connections.BroadcastCommandAsync("{\"cmd\":\"rescan_devices\"}");
            return delivered == 0
                ? Results.Ok(new { gateways = 0, message = "No gateway is connected right now" })
                : Results.Ok(new { gateways = delivered });
        });

        group.MapDelete("/{deviceId}", async (string deviceId, DeviceRepository repository) =>
        {
            var deleted = await repository.DeleteAsync(deviceId);
            return deleted ? Results.NoContent() : Results.NotFound();
        }).RequireAuthorization(Capabilities.ManageDevices);
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
