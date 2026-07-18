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

        return new BedReading(
            bedId, room, spo2, heartRate, temperature, dripRate, DateTime.UtcNow,
            flowRate, heartRateSignal, spo2Signal, flowSignal, dripRateSignal,
            lineBlocked, aeAlarm);
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
