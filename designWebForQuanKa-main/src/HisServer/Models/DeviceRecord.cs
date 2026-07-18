namespace HisServer.Models;

public enum DeviceType
{
    Xg26,
    Gateway,
    Ble
}

public enum DeviceStatus
{
    Online,
    Pending,
    Warning,
    Offline
}

public sealed class DeviceRecord
{
    public required string DeviceId { get; init; }
    public DeviceType DeviceType { get; set; }
    public string? AssignedBedId { get; set; }
    public string? Room { get; set; }
    public DeviceStatus Status { get; set; } = DeviceStatus.Pending;
    public int? BatteryPercent { get; set; }
    public int? Rssi { get; set; }
    public string? Eui64 { get; set; }
    public DateTime? LastSeenAt { get; set; }
}
