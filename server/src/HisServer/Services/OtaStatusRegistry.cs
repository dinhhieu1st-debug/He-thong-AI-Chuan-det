using System.Collections.Concurrent;
using HisServer.Domain;
using HisServer.Models;
using Microsoft.Extensions.Logging;

namespace HisServer.Services;

/// <summary>
/// The one place that knows how far each device is through a firmware update.
///
/// In memory on purpose - see the note on <see cref="OtaStatus"/>. A firmware
/// transfer does not survive a server restart, so neither should the record of
/// it: showing a technician "updating, 40%" for a transfer that died with the
/// process is worse than showing nothing.
///
/// Everything here is keyed by <see cref="DeviceIdentity.Canonicalize"/>, not
/// the raw string a caller happened to pass - "0x64028ffffe641802" and
/// "64028FFFFE641802" must land on the same entry, or one device can end up
/// with two independent OTA trackers that never agree.
/// </summary>
public sealed class OtaStatusRegistry
{
    private readonly ILogger<OtaStatusRegistry> log;

    public OtaStatusRegistry(ILogger<OtaStatusRegistry> log)
    {
        this.log = log;
    }

    private readonly ConcurrentDictionary<string, OtaStatus> statuses = new(StringComparer.Ordinal);

    public OtaStatus Get(string deviceId) =>
        statuses.TryGetValue(DeviceIdentity.Canonicalize(deviceId), out var s) ? s : OtaStatus.Unknown(deviceId);

    public IReadOnlyCollection<OtaStatus> All() => statuses.Values.ToList();

    /* A device the server itself just told to update, between the moment the
     * gateway command was accepted and the moment the gateway's own first
     * status line for it arrives. Deliberately NOT the visible OtaStatus.State
     * - the existing Starting/Updating history-and-broadcast flow already
     * fires correctly off the gateway's real "starting" line, and duplicating
     * that here would double-fire it. This dictionary exists for exactly two
     * things: closing the double-click race (see MarkUpdateRequested), and
     * proving a later Done/Failed for THIS device is solicited (see Update()).
     *
     * Per device, keyed the same canonical way as everything else here - never
     * a single "currentOtaDeviceId" global, which stops meaning anything the
     * moment a second bed starts updating at the same time. */
    private readonly ConcurrentDictionary<string, DateTime> pendingUpdates = new(StringComparer.Ordinal);

    /// <summary>
    /// Records that THIS device was just told to update, closing the window
    /// between accepting the request and the gateway's own confirmation - so a
    /// second click on the same device is rejected even before any ota_status
    /// line has come back, and so a Done/Failed that arrives in that same
    /// window is still recognised as solicited.
    /// </summary>
    public void MarkUpdateRequested(string deviceId) =>
        pendingUpdates[DeviceIdentity.Canonicalize(deviceId)] = DateTime.UtcNow;

    /// <summary>True while an update is running, or has been requested but the
    /// gateway has not yet confirmed it started. What the "Update" button's
    /// double-submit guard should check - <see cref="OtaStatus.InFlight"/>
    /// alone has a gap right after the request is sent.</summary>
    public bool IsUpdateInFlightOrPending(string deviceId) =>
        pendingUpdates.ContainsKey(DeviceIdentity.Canonicalize(deviceId)) || Get(deviceId).InFlight;

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
        var now = DateTime.UtcNow;
        var key = DeviceIdentity.Canonicalize(deviceId);
        var wasPending = pendingUpdates.TryRemove(key, out _);
        previousState = statuses.TryGetValue(key, out var existing)
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
            versions.AddOrUpdate(key,
                _ => (idle ? installedVersion : null, latestVersion),
                (_, old) => (idle ? (installedVersion ?? old.Installed) : old.Installed,
                             latestVersion ?? old.Latest));
        }

        /* DONE means "the update this device was running just finished" - it
         * must never be believed from a device with nothing on record to
         * finish. zigbee2mqtt (or a gateway carrying more than one device on
         * one connection) can and does republish state for devices nobody
         * asked about; without this a stray Done for device B, arriving while
         * device A is genuinely mid-update, would mark B "Updated" and write
         * B a false firmware-history row. Failed gets the equivalent guard
         * only at the history layer (BedTcpIngestionService.RecordOtaHistoryAsync)
         * because "Failed" is still meaningful UI feedback for a plain check
         * that timed out; Done is not similarly safe to show unconditionally -
         * it is the one state a technician reads as "safe to walk away". */
        bool DoneIsSolicited(OtaState fromState) =>
            fromState is OtaState.Starting or OtaState.Updating || wasPending;

        if (state == OtaState.Done && !DoneIsSolicited(previousState))
        {
            log.LogWarning(
                "Ignoring unsolicited OTA Done for device {DeviceId}: no active update operation (was {PreviousState}).",
                deviceId, previousState);
            return existing ?? OtaStatus.Unknown(deviceId);
        }

        return statuses.AddOrUpdate(
            key,
            _ => new OtaStatus(deviceId, state, progress, remainingSeconds, message, now),
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

                /* remainingSeconds needs the same treatment, but simply
                 * re-publishing the old number verbatim (the previous
                 * behaviour) makes it look frozen for however long the
                 * gateway goes without a fresh estimate - and the frontend
                 * re-anchors its own countdown on every message it receives,
                 * so a stale number here would make the displayed countdown
                 * visibly jump BACK UP each time one of those messages
                 * arrives. Decay it by wall-clock time actually elapsed since
                 * the previous estimate instead, so every message - fresh
                 * estimate or not - carries a number that keeps counting
                 * down. Only while still in the same state: a state change
                 * (e.g. Updating -> Failed) means the old estimate no longer
                 * describes anything real. */
                int? keptRemaining;
                if (remainingSeconds is not null)
                {
                    keptRemaining = Math.Max(0, remainingSeconds.Value);
                }
                else if (state == previous.State && previous.RemainingSeconds is not null)
                {
                    var elapsedSeconds = Math.Max(0, (int)(now - previous.UpdatedAt).TotalSeconds);
                    keptRemaining = Math.Max(0, previous.RemainingSeconds.Value - elapsedSeconds);
                }
                else
                {
                    keptRemaining = null;
                }

                return new OtaStatus(
                    deviceId, state, keptProgress, keptRemaining,
                    string.IsNullOrWhiteSpace(message) ? previous.Message : message,
                    now);
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
        new(StringComparer.Ordinal);

    public (int? Installed, int? Latest) VersionsFor(string deviceId) =>
        versions.TryGetValue(DeviceIdentity.Canonicalize(deviceId), out var v) ? v : (null, null);

    /// <summary>
    /// Forgets a device, so a removed-and-re-added device does not inherit the
    /// OTA state of its previous life.
    /// </summary>
    public void Forget(string deviceId)
    {
        var key = DeviceIdentity.Canonicalize(deviceId);
        statuses.TryRemove(key, out _);
        pendingUpdates.TryRemove(key, out _);
    }
}
