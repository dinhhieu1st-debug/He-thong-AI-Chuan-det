using HisServer.Domain;

namespace HisServer.Models;

public sealed class AlertRecord
{
    public long AlertId { get; init; }
    public required string BedId { get; init; }
    public string? Room { get; init; }
    public string? DeviceId { get; init; }
    public BedStatus Level { get; init; }
    public required string AlertType { get; init; }
    public required string Message { get; init; }
    public int? Spo2 { get; init; }
    public int? HeartRate { get; init; }
    public int? DripRate { get; init; }
    public bool Acknowledged { get; set; }
    public DateTime? AcknowledgedAt { get; set; }
    public string? AcknowledgedBy { get; set; }
    public string? AcknowledgementNote { get; set; }
    public DateTime CreatedAt { get; init; }
}
