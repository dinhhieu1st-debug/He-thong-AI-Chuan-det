using HisServer.Data;
using HisServer.Domain;
using HisServer.Hubs;
using HisServer.Models;
using HisServer.Services;
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

        /* --- Firmware over the air -----------------------------------------
         *
         * Behind ManageDevices: flashing firmware is a technician's job, and a
         * failed flash on a bedside monitor takes the bed out of service. A
         * nurse must not be able to reach this by URL.
         *
         * The server does not carry the image and does not know the protocol -
         * zigbee2mqtt owns both. These two endpoints only forward an intent
         * down to the gateways; progress comes back the other way as
         * ota_status lines and reaches the browser over SignalR. */
        group.MapPost("/{deviceId}/ota/check", async (
            string deviceId, BedConnectionRegistry connections) =>
        {
            var delivered = await connections.BroadcastCommandAsync(
                $"{{\"cmd\":\"ota_check\",\"deviceId\":\"{deviceId}\"}}");
            return Results.Ok(new { deviceId, gatewaysNotified = delivered });
        }).RequireAuthorization(Capabilities.ManageDevices);

        group.MapPost("/{deviceId}/ota/update", async (
            string deviceId,
            BedConnectionRegistry connections,
            OtaStatusRegistry ota) =>
        {
            /* Refuse a second update while one is running. Pressing Update
             * twice would start a second image transfer on top of the first,
             * and that is the one way this feature could leave a bedside
             * device holding half a firmware. The UI hides the button too, but
             * the UI is not the place to enforce it. */
            var current = ota.Get(deviceId);
            if (current.InFlight)
            {
                return Results.Conflict(new
                {
                    deviceId,
                    message = "An update is already running for this device.",
                    state = current.State.ToString(),
                    progress = current.Progress,
                });
            }

            var delivered = await connections.BroadcastCommandAsync(
                $"{{\"cmd\":\"ota_update\",\"deviceId\":\"{deviceId}\"}}");

            if (delivered == 0)
            {
                return Results.Problem(
                    "No gateway is connected, so the update could not be started.",
                    statusCode: StatusCodes.Status503ServiceUnavailable);
            }

            return Results.Accepted($"/api/devices/{deviceId}/ota",
                                    new { deviceId, gatewaysNotified = delivered });
        }).RequireAuthorization(Capabilities.ManageDevices);

        group.MapGet("/{deviceId}/ota", (string deviceId, OtaStatusRegistry ota) =>
            Results.Ok(OtaStatusDto.From(ota.Get(deviceId))))
            .RequireAuthorization(Capabilities.ManageDevices);

        /* Every known OTA state at once, for a page that has just loaded.
         *
         * Progress arrives over SignalR, so a browser opened mid-update would
         * otherwise show "Chưa kiểm tra" for a device that is actually being
         * flashed - and the Update button along with it. Reloading a page must
         * never be what lets someone start a second transfer. */
        group.MapGet("/ota", (OtaStatusRegistry ota) =>
            Results.Ok(ota.All().Select(OtaStatusDto.From)))
            .RequireAuthorization(Capabilities.ManageDevices);

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
