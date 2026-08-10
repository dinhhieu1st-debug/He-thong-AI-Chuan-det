using HisServer.Models;

namespace HisServer.Domain;

/// <summary>
/// The single canonical implementation of the vitals-to-status thresholds.
/// Replaces the 5+ duplicated (and in one case inconsistent) copies of this
/// logic found across the old WinForms views.
///
/// Status is driven by two kinds of signal: hard vitals thresholds (SpO2/HR)
/// and equipment-state flags reported by the firmware (IV line blocked/free-flow,
/// AI autoencoder anomaly, and per-channel sensor connectivity). A bed with a
/// disconnected sensor is not "Stable" — it is unmonitored, which is exactly
/// the condition a nurse needs to be alerted to.
///
/// A vitals threshold is only ever applied to a channel that is actually
/// reporting. With no probe on the patient the firmware sends 0 with the
/// channel's signal flag cleared, and reading that as "SpO2 0% — critical"
/// produced a red alarm for a bed nobody was even connected to, hiding the
/// real condition (the sensor is unplugged) behind a fake one.
/// </summary>
public static class VitalsStatusEvaluator
{
    private const int CriticalSpo2 = 90;
    private const int WarningSpo2 = 95;
    private const int LowHeartRate = 60;
    private const int HighHeartRate = 110;

    public static BedStatus Evaluate(BedReading reading)
    {
        if (IsCriticalSpo2(reading) || reading.LineBlocked)
        {
            return BedStatus.Critical;
        }

        if (IsWarningSpo2(reading)
            || IsAbnormalHeartRate(reading)
            || reading.AeAlarm
            || HasLostSignal(reading))
        {
            return BedStatus.Warning;
        }

        return BedStatus.Stable;
    }

    /// <summary>
    /// Builds the human-readable alert message and alert-type code for a
    /// non-stable reading.
    ///
    /// EVERY active cause is listed, most severe first, rather than only the
    /// first one matched: a bed can be simultaneously blocked, anomalous and
    /// half-disconnected, and a nurse reading "Critically low SpO2" alone
    /// would go on to miss the blocked line. The alert-type code still
    /// reflects the single most severe cause, since it keys the alert
    /// history and the mobile push category.
    /// </summary>
    public static (string AlertType, string Message) DescribeAlert(BedReading reading)
    {
        var causes = new List<(string Type, string Text)>();

        if (IsCriticalSpo2(reading))
        {
            causes.Add(("SPO2_LOW_CRITICAL", $"Critically low SpO2: {reading.Spo2}%"));
        }

        if (reading.LineBlocked)
        {
            causes.Add(("LINE_BLOCKED", "IV line blocked or free-flowing — check tubing immediately"));
        }

        if (IsWarningSpo2(reading))
        {
            causes.Add(("SPO2_LOW", $"Low SpO2: {reading.Spo2}%"));
        }

        if (IsAbnormalHeartRate(reading))
        {
            var direction = reading.HeartRate < LowHeartRate ? "Low" : "High";
            causes.Add(("HEART_RATE_ABNORMAL", $"{direction} heart rate: {reading.HeartRate} bpm"));
        }

        if (reading.AeAlarm)
        {
            causes.Add(("AE_ALARM", "AI model flagged an abnormal drip pattern"));
        }

        if (HasLostSignal(reading))
        {
            causes.Add(("SENSOR_DISCONNECTED", $"No signal from: {DescribeLostChannels(reading)}"));
        }

        if (causes.Count == 0)
        {
            return ("VITAL_WARNING", "Vitals require attention");
        }

        return (causes[0].Type, string.Join(" · ", causes.Select(c => c.Text)));
    }

    /// <summary>
    /// A channel only counts against the patient when its sensor is actually
    /// reporting — see the class comment.
    /// </summary>
    private static bool IsCriticalSpo2(BedReading reading) =>
        reading.Spo2Signal && reading.Spo2 < CriticalSpo2;

    private static bool IsWarningSpo2(BedReading reading) =>
        reading.Spo2Signal && reading.Spo2 >= CriticalSpo2 && reading.Spo2 < WarningSpo2;

    private static bool IsAbnormalHeartRate(BedReading reading) =>
        reading.HeartRateSignal
        && (reading.HeartRate < LowHeartRate || reading.HeartRate > HighHeartRate);

    private static bool HasLostSignal(BedReading reading) =>
        !reading.HeartRateSignal || !reading.Spo2Signal || !reading.FlowSignal || !reading.DripRateSignal;

    private static string DescribeLostChannels(BedReading reading)
    {
        var lost = new List<string>();
        if (!reading.HeartRateSignal) lost.Add("HR");
        if (!reading.Spo2Signal) lost.Add("SpO2");
        if (!reading.FlowSignal) lost.Add("Flow");
        if (!reading.DripRateSignal) lost.Add("Drip");
        return string.Join(", ", lost);
    }
}
