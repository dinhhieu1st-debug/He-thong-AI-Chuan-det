namespace HisServer.Models;

// Wire DTOs shared by the REST API responses and the SignalR broadcast payloads.

public sealed record BedDto(
    string BedId,
    string Room,
    string Status,
    int? Spo2,
    int? HeartRate,
    double? Temperature,
    int? DripRate,
    int? FlowRate,
    bool HeartRateSignal,
    bool Spo2Signal,
    bool FlowSignal,
    bool DripRateSignal,
    bool LineBlocked,
    bool AeAlarm,
    int? WeightG,
    int? DropsPerMin,
    int? TargetFlowMlH,
    int? TargetDropsPerMin,
    bool TareInProgress,
    bool TareJustCompleted,
    bool HrBaselineJustCompleted,
    int? HrBaselineSecondsRemaining,
    int? HrBaselineBpm,
    DateTime? HrBaselineCapturedAt,
    DateTime? LastTareCompletedAt,
    string? AlertMessage,
    string? DeviceId,
    DateTime? LastUpdated)
{
    public static BedDto From(BedState bed) => new(
        bed.BedId,
        bed.Room,
        bed.Status.ToString(),
        bed.Spo2,
        bed.HeartRate,
        bed.Temperature,
        bed.DripRate,
        bed.FlowRate,
        bed.HeartRateSignal,
        bed.Spo2Signal,
        bed.FlowSignal,
        bed.DripRateSignal,
        bed.LineBlocked,
        bed.AeAlarm,
        bed.WeightG,
        bed.DropsPerMin,
        bed.TargetFlowMlH,
        bed.TargetDropsPerMin,
        bed.TareInProgress,
        bed.TareJustCompleted,
        bed.HrBaselineJustCompleted,
        bed.HrBaselineSecondsRemaining,
        bed.HrBaselineBpm,
        bed.HrBaselineCapturedAt,
        bed.LastTareCompletedAt,
        bed.AlertMessage,
        bed.DeviceId,
        bed.LastDataAt);
}

public sealed record AlertDto(
    long Id,
    string BedId,
    string? Room,
    string Level,
    string AlertType,
    string Message,
    int? Spo2,
    int? HeartRate,
    int? DripRate,
    DateTime CreatedAt,
    bool Acknowledged,
    DateTime? AcknowledgedAt)
{
    public static AlertDto From(AlertRecord alert) => new(
        alert.AlertId,
        alert.BedId,
        alert.Room,
        alert.Level.ToString(),
        alert.AlertType,
        alert.Message,
        alert.Spo2,
        alert.HeartRate,
        alert.DripRate,
        alert.CreatedAt,
        alert.Acknowledged,
        alert.AcknowledgedAt);
}

public sealed record DeviceDto(
    string DeviceId,
    string DeviceType,
    string? AssignedBedId,
    string? Room,
    string Status,
    int? BatteryPercent,
    int? Rssi,
    string? Eui64,
    DateTime? LastSeenAt)
{
    public static DeviceDto From(DeviceRecord device) => new(
        device.DeviceId,
        device.DeviceType.ToString(),
        device.AssignedBedId,
        device.Room,
        device.Status.ToString(),
        device.BatteryPercent,
        device.Rssi,
        device.Eui64,
        device.LastSeenAt);
}

public sealed record LogEntryDto(string BedId, string? Room, string Category, string Level, string Message, DateTime OccurredAt)
{
    public static LogEntryDto From(LogEntry entry) => new(
        entry.BedId, entry.Room, entry.Category.ToString(), entry.Level, entry.Message, entry.OccurredAt);
}
