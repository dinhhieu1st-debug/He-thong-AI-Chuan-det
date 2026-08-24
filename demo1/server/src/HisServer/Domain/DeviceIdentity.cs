namespace HisServer.Domain;

public static class DeviceIdentity
{
    public static string Normalize(string value)
    {
        var trimmed = value.Trim();
        var compact = trimmed.StartsWith("0x", StringComparison.OrdinalIgnoreCase)
            ? trimmed[2..]
            : trimmed;
        if (compact.Length == 16 && compact.All(Uri.IsHexDigit))
        {
            return "0x" + compact.ToLowerInvariant();
        }
        return trimmed;
    }

    public static string Compact(string value)
    {
        var normalized = Normalize(value);
        return normalized.StartsWith("0x", StringComparison.OrdinalIgnoreCase)
            ? normalized[2..]
            : normalized;
    }
}
