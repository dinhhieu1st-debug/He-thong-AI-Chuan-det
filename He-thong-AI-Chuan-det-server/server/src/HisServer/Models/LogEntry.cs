namespace HisServer.Models;

public enum LogCategory
{
    Alert,
    Vital
}

/// <summary>A single row in the merged system log feed (alerts ∪ vital_samples).</summary>
public sealed class LogEntry
{
    public required string BedId { get; init; }
    public string? Room { get; init; }
    public LogCategory Category { get; init; }
    public required string Level { get; init; }
    public required string Message { get; init; }
    public DateTime OccurredAt { get; init; }
}
