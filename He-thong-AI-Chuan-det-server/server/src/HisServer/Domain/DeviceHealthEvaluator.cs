using HisServer.Models;

namespace HisServer.Domain;

public sealed class DeviceHealthOptions
{
    /// <summary>
    /// How long a channel must stay silent before the device is called faulty
    /// rather than momentarily noisy.
    ///
    /// Five minutes, because the short outages are normal: a nurse takes the
    /// SpO2 clip off to measure blood pressure, a patient rolls onto the
    /// finger, the scale is unloaded while the bag is changed. Reporting those
    /// as equipment faults would fill the technician's queue with things that
    /// fixed themselves - and a queue that is mostly noise stops being read,
    /// which is how the real fault gets missed.
    /// </summary>
    public int SensorFaultSeconds { get; set; } = 300;
}

/// <summary>
/// Decides what state a bedside device is in, from the readings it is (or is
/// not) sending.
///
/// This exists because the Devices tab used to answer "is this device healthy"
/// from a column somebody typed by hand. Nothing checked it against reality, so
/// a device could sit at ONLINE for a week after its battery died.
///
/// The three live states map onto three different technician responses, which
/// is the only reason to distinguish them:
///   Online      - nothing to do.
///   SensorFault - the unit is alive and talking; bring a probe or a cable.
///   Offline     - nothing is arriving; bring the whole kit, check power and
///                 the coordinator.
/// </summary>
public static class DeviceHealthEvaluator
{
    /// <summary>Channel names as they appear in ChannelsLost, in a fixed order
    /// so the string is stable and can be compared to detect real change.</summary>
    private static readonly (string Name, Func<BedReading, bool> HasSignal)[] Channels =
    {
        ("HR", r => r.HeartRateSignal),
        ("SPO2", r => r.Spo2Signal),
        ("FLOW", r => r.FlowSignal),
        ("DROPS", r => r.DripRateSignal)
    };

    /// <summary>
    /// Channels without signal in this reading, as "SPO2,FLOW" - or null when
    /// everything is reading.
    /// </summary>
    public static string? LostChannels(BedReading reading)
    {
        var lost = Channels.Where(c => !c.HasSignal(reading)).Select(c => c.Name).ToArray();
        return lost.Length == 0 ? null : string.Join(',', lost);
    }

    /// <summary>
    /// Health for a device that just sent <paramref name="reading"/>.
    ///
    /// <paramref name="lostSince"/> is when the currently-lost set of channels
    /// first went quiet; null when nothing is lost. The caller keeps that clock
    /// because a single reading cannot tell a five-minute outage from a
    /// one-second one.
    /// </summary>
    public static DeviceStatus Evaluate(BedReading reading, DateTime? lostSince, DateTime now,
                                        DeviceHealthOptions options)
    {
        if (LostChannels(reading) is null)
        {
            return DeviceStatus.Online;
        }

        if (lostSince is null)
        {
            // Just went quiet - not a fault yet.
            return DeviceStatus.Online;
        }

        return (now - lostSince.Value).TotalSeconds >= options.SensorFaultSeconds
            ? DeviceStatus.SensorFault
            : DeviceStatus.Online;
    }

    /// <summary>
    /// Health for a device that has sent nothing recently. Uses the SAME
    /// threshold the ward view uses to call a bed offline
    /// (Offline.ThresholdSeconds), so a bed and its device can never disagree
    /// about whether data is arriving.
    /// </summary>
    public static bool IsStale(DateTime? lastDataAt, DateTime now, int offlineThresholdSeconds) =>
        lastDataAt is null || (now - lastDataAt.Value).TotalSeconds > offlineThresholdSeconds;
}
