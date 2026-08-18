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
    public OtaStatus Update(string deviceId, string? rawState, int? progress,
                            int? remainingSeconds, string? message)
    {
        var state = OtaStatus.ParseState(rawState);

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

    /// <summary>
    /// Forgets a device, so a removed-and-re-added device does not inherit the
    /// OTA state of its previous life.
    /// </summary>
    public void Forget(string deviceId) => statuses.TryRemove(deviceId, out _);
}
