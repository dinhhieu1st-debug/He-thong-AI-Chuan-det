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
        var temperature = ReadDouble(root, 0, "nhietDo", "temperature", "temp");
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
        var dropsForecastTrusted = ReadBool(root, true, "dropsForecastTrusted", "drops_forecast_trusted");

        return new BedReading(
            bedId, room, spo2, heartRate, temperature, dripRate, DateTime.UtcNow,
            flowRate, heartRateSignal, spo2Signal, flowSignal, dripRateSignal,
            lineBlocked, aeAlarm, weightG, dropsPerMin, targetFlowMlH, targetDropsPerMin,
            tareInProgress, tareJustCompleted, hrBaselineJustCompleted, hrBaselineSecondsRemaining,
            hrBaselineBpm, tareEventCount, hrBaselineEventCount,
            tsReady, tsAnomaly, tsEarlyWarning, tsTrend, hrForecast16s,
            spo2Forecast16s, hrTrendBpmPerMin, tsAnomalyScoreX100,
            dropsTrend, dropsTrendDpmPerMin, dropsForecast16s,
            hrForecastTrusted, dropsForecastTrusted, linkQuality);
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

            if (property.ValueKind == JsonValueKind.Number && property.TryGetInt32(out var value))
            {
                return value;
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

            if (property.ValueKind == JsonValueKind.Number && property.TryGetInt32(out var value))
            {
                return value;
            }

            if (int.TryParse(property.ToString(), NumberStyles.Integer, CultureInfo.InvariantCulture, out var parsed))
            {
                return parsed;
            }
        }

        return null;
    }

    private static double ReadDouble(JsonElement root, double defaultValue, params string[] names)
    {
        foreach (var name in names)
        {
            if (!TryGetProperty(root, name, out var property))
            {
                continue;
            }

            if (property.ValueKind == JsonValueKind.Number && property.TryGetDouble(out var value))
            {
                return value;
            }

            if (double.TryParse(property.ToString(), NumberStyles.Float, CultureInfo.InvariantCulture, out var parsed))
            {
                return parsed;
            }
        }

        return defaultValue;
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
