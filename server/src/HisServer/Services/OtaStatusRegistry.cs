using System.Collections.Concurrent;
using HisServer.Models;

namespace HisServer.Services;

/// <summary>
/// The one place that knows how far each device is through a firmware update.
///
/// In memory on purpose - see the note on <see cref="OtaStatus"/>. A firmware
/// transfer does not survive a server restart, so neither should the record of
/// it: showing a technician "updating, 40%" for a transfer that died with the
/// process is worse than showing nothing.
/// </summary>
public sealed class OtaStatusRegistry
{
    private readonly ConcurrentDictionary<string, OtaStatus> statuses =
        new(StringComparer.OrdinalIgnoreCase);

    public OtaStatus Get(string deviceId) =>
        statuses.TryGetValue(deviceId, out var s) ? s : OtaStatus.Unknown(deviceId);

    public IReadOnlyCollection<OtaStatus> All() => statuses.Values.ToList();

    /// <summary>
    /// Records what the gateway reported. Returns the stored status.
    /// </summary>
    /// <summary>
    /// Records what the gateway reported, and reports back what CHANGED.
    /// </summary>
    /// <param name="previousState">
    /// The state this device was in before this message. The caller uses it to
    /// write a history entry only on a real transition - the device republishes
    /// its state about once a second, and logging every message would bury the
    /// three events that matter under thousands that do not.
    /// </param>
    public OtaStatus Update(string deviceId, string? rawState, int? progress,
                            int? remainingSeconds, string? message,
                            out OtaState previousState,
                            int? installedVersion = null, int? latestVersion = null)
    {
        var state = OtaStatus.ParseState(rawState);
        previousState = statuses.TryGetValue(deviceId, out var existing)
            ? existing.State
            : OtaState.Unknown;

        /* The version a device is RUNNING is only believed while it is idle.
         *
         * Otherwise the history reads "v4 -> v4": by the time the gateway
         * reports the update finished, the device has already rebooted into
         * the new image and is announcing the new number, so the version it
         * came FROM is gone. Freezing it for the duration of an update keeps
         * the one fact the history exists to record.
         *
         * The offered version is taken whenever it arrives - that one does not
         * change under us. */
        var idle = state is OtaState.Unknown or OtaState.UpToDate or OtaState.Available;

        if ((installedVersion is not null && idle) || latestVersion is not null)
        {
            versions.AddOrUpdate(deviceId,
                _ => (idle ? installedVersion : null, latestVersion),
                (_, old) => (idle ? (installedVersion ?? old.Installed) : old.Installed,
                             latestVersion ?? old.Latest));
        }

        return statuses.AddOrUpdate(
            deviceId,
            _ => new OtaStatus(deviceId, state, progress, remainingSeconds,
                               message, DateTime.UtcNow),
            (_, previous) =>
            {
                /* Keep the last known percentage when a message arrives without
                 * one.
                 *
                 * zigbee2mqtt emits plenty of device messages during a transfer
                 * that carry the update state but no progress field. Writing
                 * null over a real 47% makes the bar collapse to empty and then
                 * jump back, which reads as a stalled or restarted update - the
                 * single most alarming thing a progress bar can do while
                 * someone is flashing a bedside monitor. */
                var keptProgress = progress ?? (state == previous.State ? previous.Progress : null);
                var keptRemaining = remainingSeconds ?? (state == previous.State ? previous.RemainingSeconds : null);

                return new OtaStatus(
                    deviceId, state, keptProgress, keptRemaining,
                    string.IsNullOrWhiteSpace(message) ? previous.Message : message,
                    DateTime.UtcNow);
            });
    }

    /* Last versions seen from each device, so a "finished" event can say what
     * it went from and to. Per device, not one pair for the whole server: with
     * two beds updating, a single pair would attribute one device's versions to
     * the other, and a firmware history that lies is worse than none.
     *
     * zigbee2mqtt reports these alongside the state, but not in the message
     * announcing completion - by then it has already moved on. */
    private readonly ConcurrentDictionary<string, (int? Installed, int? Latest)> versions =
        new(StringComparer.OrdinalIgnoreCase);

    public (int? Installed, int? Latest) VersionsFor(string deviceId) =>
        versions.TryGetValue(deviceId, out var v) ? v : (null, null);

    /// <summary>
    /// Forgets a device, so a removed-and-re-added device does not inherit the
    /// OTA state of its previous life.
    /// </summary>
    public void Forget(string deviceId) => statuses.TryRemove(deviceId, out _);
}
