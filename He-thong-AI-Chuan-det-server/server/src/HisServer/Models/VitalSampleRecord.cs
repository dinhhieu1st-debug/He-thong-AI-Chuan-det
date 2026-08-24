using HisServer.Domain;

namespace HisServer.Models;

public sealed class VitalSampleRecord
{
    public long SampleId { get; init; }
    public required string BedId { get; init; }
    public string? DeviceId { get; init; }
    public int? Spo2 { get; init; }
    public int? HeartRate { get; init; }
    public int? DripRate { get; init; }
    public BedStatus Status { get; init; }
    public DateTime RecordedAt { get; init; }
}
