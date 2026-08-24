using HisServer.Data;
using HisServer.Hubs;
using HisServer.Models;
using Microsoft.AspNetCore.SignalR;
using Microsoft.Extensions.Options;

namespace HisServer.Domain;

/// <summary>
/// Periodically flips any device that has gone quiet to Offline - the device
/// counterpart of <see cref="OfflineScanService"/>, which already does this
/// for beds.
///
/// Before this existed, a device's Online/SensorFault status was only ever
/// written by <c>BedTcpIngestionService.UpdateDeviceHealthAsync</c>, which
/// only runs when a reading actually arrives. Pull the power on a device and
/// nothing runs that path again, so it could sit at ONLINE forever - the
/// exact bug <see cref="DeviceHealthEvaluator"/>'s own doc comment says this
/// feature exists to prevent, just not closed for the "stopped sending
/// entirely" case.
///
/// Reuses <see cref="OfflineOptions"/> (same threshold/interval as the bed
/// scanner) rather than a second config section: "no data within N seconds"
/// is the same fact for a bed and for the device reporting to it, and the two
/// should never be tuned to disagree.
/// </summary>
public sealed class DeviceOfflineScanService : BackgroundService
{
    private readonly DeviceRepository deviceRepository;
    private readonly IHubContext<MonitoringHub, IMonitoringClient> hub;
    private readonly IOptionsMonitor<OfflineOptions> options;
    private readonly ILogger<DeviceOfflineScanService> logger;

    public DeviceOfflineScanService(
        DeviceRepository deviceRepository,
        IHubContext<MonitoringHub, IMonitoringClient> hub,
        IOptionsMonitor<OfflineOptions> options,
        ILogger<DeviceOfflineScanService> logger)
    {
        this.deviceRepository = deviceRepository;
        this.hub = hub;
        this.options = options;
        this.logger = logger;
    }

    protected override async Task ExecuteAsync(CancellationToken stoppingToken)
    {
        while (!stoppingToken.IsCancellationRequested)
        {
            var interval = TimeSpan.FromSeconds(Math.Max(1, options.CurrentValue.ScanIntervalSeconds));
            var threshold = TimeSpan.FromSeconds(Math.Max(1, options.CurrentValue.ThresholdSeconds));
            var now = DateTime.UtcNow;

            try
            {
                var devices = await deviceRepository.GetAllAsync(stoppingToken);

                foreach (var device in devices)
                {
                    /* Already Offline: nothing to flip.
                     *
                     * Pending: never reported at all yet (fresh join, or never
                     * assigned) - "no data" is expected, not a fault, so this
                     * scanner leaves it alone rather than reclassifying "not
                     * connected yet" as "went offline".
                     *
                     * Gateway: never sends a BedReading in the first place, so
                     * LastDataAt never gets set through this path at all. This
                     * scanner does not cover gateway health - that would need
                     * its own signal (a zigbee2mqtt bridge/state heartbeat),
                     * which is a separate, not-yet-built feature. Treating a
                     * gateway's permanently-null LastDataAt as "offline" would
                     * flag every gateway on the very first scan tick. */
                    if (device.Status == DeviceStatus.Offline
                        || device.Status == DeviceStatus.Pending
                        || device.DeviceType == DeviceType.Gateway)
                    {
                        continue;
                    }

                    if (device.LastDataAt is null || now - device.LastDataAt.Value <= threshold)
                    {
                        continue;
                    }

                    try
                    {
                        await deviceRepository.UpdateHealthAsync(
                            device.DeviceId, DeviceStatus.Offline,
                            device.LinkQuality, device.ChannelsLost, device.LastDataAt.Value, stoppingToken);
                        await deviceRepository.AddEventAsync(
                            device.DeviceId, device.AssignedBedId, DeviceEventType.Offline, null, stoppingToken);

                        device.Status = DeviceStatus.Offline;
                        await hub.Clients.Group(MonitoringHub.DeviceGroup).DeviceUpdated(DeviceDto.From(device));
                    }
                    catch (Exception ex)
                    {
                        logger.LogWarning(ex, "Failed to persist offline transition for device {DeviceId}.",
                            device.DeviceId);
                    }
                }
            }
            catch (Exception ex)
            {
                logger.LogWarning(ex, "Device offline scan failed.");
            }

            try
            {
                await Task.Delay(interval, stoppingToken);
            }
            catch (OperationCanceledException)
            {
                break;
            }
        }
    }
}
