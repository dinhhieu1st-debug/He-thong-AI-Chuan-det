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
/// </summary>
public static class VitalsStatusEvaluator
{
    private const int CriticalSpo2 = 90;
    private const int WarningSpo2 = 95;
    private const int LowHeartRate = 60;
    private const int HighHeartRate = 110;

    public static BedStatus Evaluate(BedReading reading)
    {
        if (reading.Spo2 < CriticalSpo2 || reading.LineBlocked)
        {
            return BedStatus.Critical;
        }

        if (reading.Spo2 < WarningSpo2
            || reading.HeartRate < LowHeartRate
            || reading.HeartRate > HighHeartRate
            || reading.AeAlarm
            || HasLostSignal(reading))
        {
            return BedStatus.Warning;
        }

        return BedStatus.Stable;
    }

    /// <summary>
    /// Builds the human-readable alert message and alert-type code for a non-stable
    /// reading, ordered most-severe-cause first (critical causes take priority).
    /// </summary>
    public static (string AlertType, string Message) DescribeAlert(BedReading reading)
    {
        if (reading.Spo2 < CriticalSpo2)
        {
            return ("SPO2_LOW_CRITICAL", $"Critically low SpO2: {reading.Spo2}%");
        }

        if (reading.LineBlocked)
        {
            return ("LINE_BLOCKED", "IV line blocked or free-flowing — check tubing immediately");
        }

        if (reading.Spo2 < WarningSpo2)
        {
            return ("SPO2_LOW", $"Low SpO2: {reading.Spo2}%");
        }

        if (reading.HeartRate < LowHeartRate || reading.HeartRate > HighHeartRate)
        {
            return ("HEART_RATE_ABNORMAL", $"Abnormal heart rate: {reading.HeartRate} bpm");
        }

        if (reading.AeAlarm)
        {
            return ("AE_ALARM", "AI model flagged an abnormal drip pattern");
        }

        if (HasLostSignal(reading))
        {
            return ("SENSOR_DISCONNECTED", $"Sensor(s) disconnected: {DescribeLostChannels(reading)}");
        }

        return ("VITAL_WARNING", "Vitals require attention");
    }

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
