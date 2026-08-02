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
}

public sealed class MonitoringHub : Hub<IMonitoringClient>
{
}
