using System.Globalization;
using System.Text.Json;
using HisServer.Models;

namespace HisServer.Ingestion;

/// <summary>
/// Parses a single newline-delimited JSON line from the bed TCP ingestion socket.
/// Field-name matching stays alias-tolerant and case-insensitive to preserve wire
/// compatibility with existing device firmware, which the new server cannot change.
/// </summary>
public static class BedDataParser
{
    public static BedReading Parse(string json)
    {
        using var document = JsonDocument.Parse(json);
        var root = document.RootElement;

        var bedId = ReadString(root, "maGiuong", "bedId", "bed_id", "bed", "giuong") ?? "Bed-01";
        var room = ReadString(root, "phong", "room") ?? "Unknown room";
        var spo2 = ReadInt(root, 0, "spo2", "SpO2", "SpO₂");
        var heartRate = ReadInt(root, 0, "nhipTim", "heartRate", "heart_rate", "bpm");
        var dripRate = ReadInt(root, 0, "tocDoNhoGiot", "dripRate", "drip_rate", "dropRate");
        var flowRate = ReadInt(root, 0, "flowRate", "flow_rate", "flow");

        // Cac co "co tin hieu" tung kenh cam bien. Mac dinh true (coi nhu co
        // tin hieu) neu thiet bi/gateway khong gui field nay, de tuong thich
        // nguoc voi firmware cu chua expose duoc trang thai tung kenh.
        var heartRateSignal = ReadBool(root, true, "heartRateSignal", "hr_signal");
        var spo2Signal = ReadBool(root, true, "spo2Signal", "spo2_signal");
        var flowSignal = ReadBool(root, true, "flowSignal", "flow_signal");
        var dripRateSignal = ReadBool(root, true, "dripRateSignal", "drops_signal");

        // Clinical/AI alarm flags forwarded straight from the firmware's alarm
        // bitmap (see empty_2/app.c) via the gateway. Default false (no alarm)
        // if the field is absent, for backward compatibility with older gateway builds.
        var lineBlocked = ReadBool(root, false, "lineBlocked", "line_blocked");
        var aeAlarm = ReadBool(root, false, "aeAlarm", "ae_alarm");

        // Raw telemetry + tare/baseline events (added alongside the doctor-
        // settable target flow rate feature) - absent on older gateway
        // builds, so these stay nullable/false rather than defaulting to a
        // potentially-misleading 0.
        var weightG = ReadNullableInt(root, "weightG", "weight_g");
        var dropsPerMin = ReadNullableInt(root, "dropsPerMin", "drops_per_min");
        var targetFlowMlH = ReadNullableInt(root, "targetFlowMlH", "target_flow_ml_h");
        var targetDropsPerMin = ReadNullableInt(root, "targetDropsPerMin", "target_drops_per_min");
        var tareInProgress = ReadBool(root, false, "tareInProgress", "tare_in_progress");
        var tareJustCompleted = ReadBool(root, false, "tareJustCompleted", "tare_just_completed");
        var hrBaselineJustCompleted = ReadBool(root, false, "hrBaselineJustCompleted", "hr_baseline_just_completed");
        var hrBaselineSecondsRemaining = ReadNullableInt(root, "hrBaselineSecondsRemaining", "hr_baseline_seconds_remaining");
        var hrBaselineBpm = ReadNullableInt(root, "hrBaselineBpm", "hr_baseline_bpm");
        var tareEventCount = ReadNullableInt(root, "tareEventCount", "tare_event_count");
        var hrBaselineEventCount = ReadNullableInt(root, "hrBaselineEventCount", "hr_baseline_event_count");

        // Ket qua cua model DU BAO chuoi thoi gian tren chip (ts_monitor.c).
        // Deu nullable: gateway/firmware cu khong gui, va ngay ca firmware moi
        // cung chi co du bao sau khi gom du cua so 64 giay dau tien.
        var tsReady = ReadBool(root, false, "tsReady", "ts_ready");
        var tsAnomaly = ReadBool(root, false, "tsAnomaly", "ts_anomaly");
        var tsEarlyWarning = ReadBool(root, false, "tsEarlyWarning", "ts_early_warning");
        var tsTrend = ReadNullableInt(root, "tsTrend", "ts_trend");
        var hrForecast16s = ReadNullableInt(root, "hrForecast16s", "hr_forecast_16s");
        var spo2Forecast16s = ReadNullableInt(root, "spo2Forecast16s", "spo2_forecast_16s");
        var hrTrendBpmPerMin = ReadNullableInt(root, "hrTrendBpmPerMin", "hr_trend_bpm_per_min");
        // Firmware gui diem da nhan 100 de giu 2 chu so thap phan qua duong so nguyen.
        var tsAnomalyScoreX100 = ReadNullableInt(root, "tsAnomalyScore", "ts_anomaly_score");
        var dropsTrend = ReadNullableInt(root, "dropsTrend", "drops_trend");
        var dropsTrendDpmPerMin = ReadNullableInt(root, "dropsTrendDpmPerMin", "drops_trend_dpm_per_min");
        var dropsForecast16s = ReadNullableInt(root, "dropsForecast16s", "drops_forecast_16s");
        // Model chi hoc tren du lieu binh thuong, nen khi kenh dang bat thuong
        // keo dai thi con so no dua ra la "muc binh thuong dang le phai co",
        // KHONG phai du bao. Hai co nay cho giao dien biet de doi nhan.
        var hrForecastTrusted = ReadBool(root, true, "hrForecastTrusted", "hr_forecast_trusted");
        // -1 is the gateway's "not present in the MQTT payload" marker.
        var linkQuality = ReadNullableInt(root, "linkQuality", "linkquality", "link_quality");
        if (linkQuality is < 0) linkQuality = null;
        var deviceId = ReadString(root, "deviceId", "device_id");
        if (string.IsNullOrWhiteSpace(deviceId)) deviceId = null;
        var dropsForecastTrusted = ReadBool(root, true, "dropsForecastTrusted", "drops_forecast_trusted");

        // AI v2. AlertLevel stays null for a v1 device rather than defaulting
        // to 0 - "this device cannot tell us" and "this device says everything
        // is fine" are different facts, and collapsing them would let an old
        // device report a permanently green bed.
        var alertLevel = ReadNullableInt(root, "alertLevel", "alert_level");
        var finalAlertLevel = ReadNullableInt(root, "finalAlertLevel", "final_alert_level");
        if (finalAlertLevel is < 1 or > 3) finalAlertLevel = null;
        var lineBranch = ReadBool(root, false, "lineBranch", "line_branch");
        var patientBranch = ReadBool(root, false, "patientBranch", "patient_branch");
        var dripAnomaly = ReadBool(root, false, "dripAnomaly", "drip_anomaly");
        var vitalsAnomaly = ReadBool(root, false, "vitalsAnomaly", "vitals_anomaly");
        // The gateway sends -1 for "the device could not work this out yet".
        var lineState = ReadNullableInt(root, "lineState", "line_state");
        if (lineState is < 0) lineState = null;
        var remainingMl = ReadNullableInt(root, "remainingMl", "remaining_ml");
        if (remainingMl is < 0) remainingMl = null;
        var remainingMin = ReadNullableInt(root, "remainingMin", "remaining_min");
        if (remainingMin is < 0) remainingMin = null;
        var dropTrainingSamples = ReadNullableInt(root, "dropTrainingSamples", "drop_training_samples");
        if (dropTrainingSamples is < 0) dropTrainingSamples = null;
        var vitalsTrainingSamples = ReadNullableInt(root, "vitalsTrainingSamples", "vitals_training_samples");
        if (vitalsTrainingSamples is < 0) vitalsTrainingSamples = null;

        return new BedReading(
            bedId, room, spo2, heartRate, dripRate, DateTime.UtcNow,
            flowRate, heartRateSignal, spo2Signal, flowSignal, dripRateSignal,
            lineBlocked, aeAlarm, weightG, dropsPerMin, targetFlowMlH, targetDropsPerMin,
            tareInProgress, tareJustCompleted, hrBaselineJustCompleted, hrBaselineSecondsRemaining,
            hrBaselineBpm, tareEventCount, hrBaselineEventCount,
            tsReady, tsAnomaly, tsEarlyWarning, tsTrend, hrForecast16s,
            spo2Forecast16s, hrTrendBpmPerMin, tsAnomalyScoreX100,
            dropsTrend, dropsTrendDpmPerMin, dropsForecast16s,
            hrForecastTrusted, dropsForecastTrusted, linkQuality, deviceId,
            alertLevel, finalAlertLevel, lineBranch, patientBranch, dripAnomaly, vitalsAnomaly,
            lineState, remainingMl, remainingMin,
            ReadBool(root, true, "monitoring"),
            dropTrainingSamples,
            vitalsTrainingSamples,
            ReadNullableBool(root, "alertsArmed", "alerts_armed"));
    }

    private static string? ReadString(JsonElement root, params string[] names)
    {
        foreach (var name in names)
        {
            if (TryGetProperty(root, name, out var property))
            {
                return property.ValueKind == JsonValueKind.String ? property.GetString() : property.ToString();
            }
        }

        return null;
    }

    private static int ReadInt(JsonElement root, int defaultValue, params string[] names)
    {
        foreach (var name in names)
        {
            if (!TryGetProperty(root, name, out var property))
            {
                continue;
            }

            if (property.ValueKind == JsonValueKind.Number)
            {
                // A whole number parses directly; a decimal (a sensor reporting
                // 98.3, say) fails TryGetInt32 - round it instead of falling
                // through to the string branch below, which silently returns
                // defaultValue for "98.3" and used to turn a real reading into a
                // fake 0 (read by VitalsStatusEvaluator as "Critical").
                if (property.TryGetInt32(out var value))
                {
                    return value;
                }
                if (property.TryGetDouble(out var real))
                {
                    return (int)Math.Round(real, MidpointRounding.AwayFromZero);
                }
            }

            if (int.TryParse(property.ToString(), NumberStyles.Integer, CultureInfo.InvariantCulture, out var parsed))
            {
                return parsed;
            }
        }

        return defaultValue;
    }

    private static int? ReadNullableInt(JsonElement root, params string[] names)
    {
        foreach (var name in names)
        {
            if (!TryGetProperty(root, name, out var property))
            {
                continue;
            }

            if (property.ValueKind == JsonValueKind.Number)
            {
                if (property.TryGetInt32(out var value))
                {
                    return value;
                }
                if (property.TryGetDouble(out var real))
                {
                    return (int)Math.Round(real, MidpointRounding.AwayFromZero);
                }
            }

            if (int.TryParse(property.ToString(), NumberStyles.Integer, CultureInfo.InvariantCulture, out var parsed))
            {
                return parsed;
            }
        }

        return null;
    }

    private static bool ReadBool(JsonElement root, bool defaultValue, params string[] names)
    {
        foreach (var name in names)
        {
            if (!TryGetProperty(root, name, out var property))
            {
                continue;
            }

            if (property.ValueKind is JsonValueKind.True or JsonValueKind.False)
            {
                return property.GetBoolean();
            }

            if (bool.TryParse(property.ToString(), out var parsed))
            {
                return parsed;
            }
        }

        return defaultValue;
    }

    private static bool? ReadNullableBool(JsonElement root, params string[] names)
    {
        foreach (var name in names)
        {
            if (!TryGetProperty(root, name, out var property)) continue;
            if (property.ValueKind is JsonValueKind.True or JsonValueKind.False)
                return property.GetBoolean();
            if (property.ValueKind == JsonValueKind.Number && property.TryGetInt32(out var number))
                return number != 0;
            if (bool.TryParse(property.ToString(), out var parsed)) return parsed;
        }
        return null;
    }

    private static bool TryGetProperty(JsonElement root, string name, out JsonElement property)
    {
        foreach (var jsonProperty in root.EnumerateObject())
        {
            if (string.Equals(jsonProperty.Name, name, StringComparison.OrdinalIgnoreCase))
            {
                property = jsonProperty.Value;
                return true;
            }
        }

        property = default;
        return false;
    }
}
