namespace HisServer.Domain;

/// <summary>
/// The one place that decides when two device id strings name the same
/// device - "0x64028ffffe641802" and "64028FFFFE641802" must canonicalize to
/// the same key, or a device can end up with two OTA registry entries, two
/// history threads, and a per-device guard that never actually recognizes an
/// operation as belonging to it.
///
/// Deliberately narrow (case + an optional "0x" prefix): loosening it further
/// (e.g. stripping other characters) risks two DIFFERENT EUI64s colliding.
/// </summary>
public static class DeviceIdentity
{
    public static string Canonicalize(string deviceId)
    {
        var value = deviceId.Trim().ToLowerInvariant();
        return value.StartsWith("0x", StringComparison.Ordinal) ? value[2..] : value;
    }
}
