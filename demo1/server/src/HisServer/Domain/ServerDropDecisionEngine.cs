using System.Collections.Concurrent;
using HisServer.Models;

namespace HisServer.Domain;

/// <summary>
/// Server-side replacement for the old Windows drip GUI.  It consumes only
/// physical drop events reported by G26 and returns level 1/2/3 to G26.  G26
/// remains the sole owner of final fusion with the vital-sign branch.
/// </summary>
public sealed class ServerDropDecisionEngine
{
    public sealed record Decision(int Level, string Reason, bool Changed);

    private sealed class State
    {
        public int? EventCount;
        public DateTime LastEventAt;
        public int Level = 1;
        public int GoodRecoveryCount;
        public int? TargetDpm;
        public double DynamicTargetMs;
        public string Reason = "Drip normal";
    }

    private readonly ConcurrentDictionary<string, State> states = new(StringComparer.OrdinalIgnoreCase);

    public Decision Evaluate(BedReading reading)
    {
        var state = states.GetOrAdd(reading.BedId, _ => new State());
        lock (state)
        {
            var target = reading.TargetDropsPerMin.GetValueOrDefault();
            if (!reading.Monitoring || !reading.AlertsArmed || target <= 0)
            {
                return Set(state, 1, !reading.Monitoring ? "Monitoring paused" : "Waiting for training/target");
            }

            if (state.TargetDpm != target)
            {
                state.TargetDpm = target;
                state.DynamicTargetMs = 60000.0 / target;
                state.EventCount = reading.DropEventCount;
                state.LastEventAt = reading.ReceivedAt;
                state.GoodRecoveryCount = 0;
                return Set(state, 1, $"Target set to {target} dpm");
            }

            var newEvent = reading.DropEventCount is int count && count != state.EventCount;
            if (newEvent)
            {
                state.EventCount = reading.DropEventCount;
                state.LastEventAt = reading.ReceivedAt;
                var setTargetMs = 60000.0 / target;
                if (state.DynamicTargetMs <= 0) state.DynamicTargetMs = setTargetMs;
                var interval = reading.DropIntervalMs.GetValueOrDefault();
                var error = interval > 0 ? Math.Abs(interval - state.DynamicTargetMs) : double.MaxValue;
                var rawLevel = error <= 200 ? 1 : error <= 800 ? 2 : 3;

                // A gravity-fed bag naturally slows by a few milliseconds per
                // drop. Follow only small, normal, SLOWER changes. Never learn
                // a sudden pulse, a missing drop or a large deviation. Both
                // per-drop adaptation and total drift from the prescribed set
                // point are bounded so the baseline cannot run away.
                if (rawLevel == 1 && interval >= state.DynamicTargetMs)
                {
                    const double maxStepMs = 5.0;
                    var maxTotalDriftMs = Math.Min(250.0, setTargetMs * 0.25);
                    var candidate = state.DynamicTargetMs
                                  + Math.Min(maxStepMs, interval - state.DynamicTargetMs);
                    state.DynamicTargetMs = Math.Min(candidate, setTargetMs + maxTotalDriftMs);
                }
                var direction = interval < state.DynamicTargetMs ? "fast" : "slow";
                var reason = rawLevel switch
                {
                    1 => $"Drip normal: {interval} ms (dynamic target {state.DynamicTargetMs:F0} ms)",
                    2 => $"Drip level 2: abnormally {direction} ({interval} ms; deviation {error:F0} ms)",
                    _ => $"Drip level 3: severely {direction} ({interval} ms; deviation {error:F0} ms)"
                };

                // Alarm recovery is deliberately slower than entry: three
                // consecutive in-tolerance physical drops prevent green/yellow
                // flicker when a folded line is released.
                if (rawLevel == 1 && state.Level > 1)
                {
                    state.GoodRecoveryCount++;
                    if (state.GoodRecoveryCount < 3)
                        return Set(state, state.Level, $"Recovering {state.GoodRecoveryCount}/3: {reason}");
                }
                else
                {
                    state.GoodRecoveryCount = 0;
                }
                return Set(state, rawLevel, reason);
            }

            var timeoutMs = Math.Max(60000.0 / target, state.DynamicTargetMs) * 2.0 + 200.0;
            if (state.LastEventAt != default &&
                (reading.ReceivedAt - state.LastEventAt).TotalMilliseconds >= timeoutMs)
            {
                state.GoodRecoveryCount = 0;
                return Set(state, 3, $"Drip level 3: missing drops for {(reading.ReceivedAt - state.LastEventAt).TotalSeconds:F1} s");
            }

            return new Decision(state.Level, state.Reason, false);
        }
    }

    public string? CurrentReason(string bedId) =>
        states.TryGetValue(bedId, out var state) ? state.Reason : null;

    private static Decision Set(State state, int level, string reason)
    {
        var changed = state.Level != level;
        state.Level = level;
        state.Reason = reason;
        return new Decision(level, reason, changed);
    }
}
