using HisServer.Domain;

namespace HisServer.Models;

/// <summary>Current snapshot of a bed — the in-memory and `beds` table source of truth.</summary>
public sealed class BedState
{
    public required string BedId { get; init; }
    public required string Room { get; set; }
    public BedStatus Status { get; set; } = BedStatus.Offline;
    public int? Spo2 { get; set; }
    public int? HeartRate { get; set; }
    public double? Temperature { get; set; }
    public int? DripRate { get; set; }
    public int? FlowRate { get; set; }
    public bool HeartRateSignal { get; set; } = true;
    public bool Spo2Signal { get; set; } = true;
    public bool FlowSignal { get; set; } = true;
    public bool DripRateSignal { get; set; } = true;
    public bool LineBlocked { get; set; }
    public bool AeAlarm { get; set; }
    public string? AlertMessage { get; set; }
    public string? DeviceId { get; set; }
    public DateTime? LastDataAt { get; set; }
}
