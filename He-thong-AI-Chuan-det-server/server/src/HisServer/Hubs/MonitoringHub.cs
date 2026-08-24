using System.Security.Claims;
using HisServer.Data;
using HisServer.Domain;
using HisServer.Models;
using Microsoft.AspNetCore.SignalR;

namespace HisServer.Hubs;

/// <summary>Push-only hub: the server broadcasts state changes, clients never call back.</summary>
public interface IMonitoringClient
{
    Task BedUpdated(BedDto bed);

    /// <summary>
    /// A bed was removed from the directory. Sent so every open console drops
    /// it immediately: a bed that no longer exists but still sits on a ward
    /// screen is a bed somebody may look for a patient in.
    /// </summary>
    Task BedRemoved(string bedId);
    Task AlertCreated(AlertDto alert);
    Task AlertAcknowledged(long alertId, DateTime? acknowledgedAt);
    Task DeviceUpdated(DeviceDto device);

    /// <summary>Firmware update progress for one device.
    ///
    /// Its own event rather than a field on DeviceUpdated: an update sends a
    /// message every few seconds for minutes, and pushing the whole device
    /// record that often would repaint the Devices tab continuously.</summary>
    Task OtaStatusChanged(OtaStatusDto status);

    /// <summary>A Zigbee device announced itself to a gateway. Distinct from
    /// DeviceUpdated so the Devices tab can highlight something that just
    /// appeared and needs a technician to assign it a bed.</summary>
    Task DeviceDiscovered(DeviceDto device);

    /// <summary>A fault report was raised, claimed or resolved. One event for
    /// all three: the payload carries the status, and the technician's queue
    /// re-renders from it either way.</summary>
    Task FaultReported(FaultReportDto report);

    /// <summary>
    /// The bed directory changed: a bed was created or removed, a room was
    /// renamed, or a device was assigned to a different bed.
    ///
    /// Carries NO payload on purpose. A directory row is a join across the bed
    /// store and the devices table (see the /api/beds/directory endpoint), and
    /// rebuilding that join here would be the same logic in a second place,
    /// free to drift from the first. The client re-fetches instead - the
    /// directory changes when an administrator does something, not many times
    /// a second, so the extra request costs nothing.
    ///
    /// Needed because administrators and technicians hold ViewBedDirectory but
    /// NOT ViewWard, so they are not in WardGroup and BedUpdated never reaches
    /// them. Before this, renaming a room left every other open directory
    /// showing the old name until somebody reloaded the page.
    /// </summary>
    Task BedDirectoryChanged();
}

/// <summary>
/// Live push to the browser.
///
/// Clients are placed into groups by CAPABILITY on connect, and every
/// broadcast goes to a group rather than to everyone. Without that, a
/// technician's socket would still receive BedUpdated - full vitals and the
/// patient's name - even though their screen shows none of it. Hiding data in
/// the UI while shipping it down the wire is not hiding it at all.
/// </summary>
public sealed class MonitoringHub : Hub<IMonitoringClient>
{
    /// <summary>Beds, vitals, clinical alerts.</summary>
    public const string WardGroup = "ward";

    /// <summary>Equipment: device health and newly joined devices.</summary>
    public const string DeviceGroup = "devices";

    /// <summary>Equipment fault reports, seen by whoever raises or handles them.</summary>
    public const string FaultGroup = "faults";

    /// <summary>Bed identifiers and rooms - no vitals, no patient. Held by
    /// nurses, technicians and administrators alike.</summary>
    public const string DirectoryGroup = "directory";

    private readonly UserRepository users;

    public MonitoringHub(UserRepository users)
    {
        this.users = users;
    }

    public override async Task OnConnectedAsync()
    {
        var idClaim = Context.User?.FindFirst(ClaimTypes.NameIdentifier)?.Value;
        if (int.TryParse(idClaim, out var userId))
        {
            // Read the role from the database, not the cookie: a role change
            // must take effect on the next connection, not at the next login.
            var user = await users.FindByIdAsync(userId);
            if (user is { IsActive: true })
            {
                if (Capabilities.Has(user.Role, Capabilities.ViewWard))
                    await Groups.AddToGroupAsync(Context.ConnectionId, WardGroup);
                if (Capabilities.Has(user.Role, Capabilities.ManageDevices))
                    await Groups.AddToGroupAsync(Context.ConnectionId, DeviceGroup);
                if (Capabilities.Has(user.Role, Capabilities.HandleFaults)
                    || Capabilities.Has(user.Role, Capabilities.ReportFaults))
                    await Groups.AddToGroupAsync(Context.ConnectionId, FaultGroup);
                if (Capabilities.Has(user.Role, Capabilities.ViewBedDirectory))
                    await Groups.AddToGroupAsync(Context.ConnectionId, DirectoryGroup);
            }
        }

        await base.OnConnectedAsync();
    }
}
