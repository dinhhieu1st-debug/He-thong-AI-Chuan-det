using HisServer.Models;
using Microsoft.AspNetCore.SignalR;

namespace HisServer.Hubs;

/// <summary>Push-only hub: the server broadcasts state changes, clients never call back.</summary>
public interface IMonitoringClient
{
    Task BedUpdated(BedDto bed);
    Task AlertCreated(AlertDto alert);
    Task AlertAcknowledged(long alertId, DateTime? acknowledgedAt);
    Task DeviceUpdated(DeviceDto device);

    /// <summary>A Zigbee device announced itself to a gateway. Distinct from
    /// DeviceUpdated so the Devices tab can highlight something that just
    /// appeared and needs a technician to assign it a bed.</summary>
    Task DeviceDiscovered(DeviceDto device);
}

public sealed class MonitoringHub : Hub<IMonitoringClient>
{
}
