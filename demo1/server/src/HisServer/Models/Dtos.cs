namespace HisServer.Models;

// Wire DTOs shared by the REST API responses and the SignalR broadcast payloads.

/// <summary>
/// One point of a bed's charted history (GET /api/beds/{bedId}/history).
/// Every numeric field is nullable on purpose - null means "no trustworthy
/// reading at that instant", which the charts draw as a gap in the line
/// rather than a misleading zero.
/// </summary>
public sealed record VitalSampleDto(
    DateTime RecordedAt,
    int? Spo2,
    int? HeartRate,
    int? DripRate,
    int? FlowRate,
    int? WeightG,
    int? DropsPerMin,
    int? TargetFlowMlH,
    int? TargetDropsPerMin,
    bool LineBlocked,
    bool AeAlarm,
    string Status);

public sealed record BedDto(
    string BedId,
    string Room,
    string Status,
    int? Spo2,
    int? HeartRate,
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
    bool TsReady,
    bool TsAnomaly,
    bool TsEarlyWarning,
    int? TsTrend,
    int? HrForecast16s,
    int? Spo2Forecast16s,
    int? HrTrendBpmPerMin,
    int? TsAnomalyScoreX100,
    int? DropsTrend,
    int? DropsTrendDpmPerMin,
    int? DropsForecast16s,
    bool HrForecastTrusted,
    bool DropsForecastTrusted,
    int? AlertLevel,
    bool LineBranch,
    bool PatientBranch,
    bool DripAnomaly,
    bool VitalsAnomaly,
    int? LineState,
    int? RemainingMl,
    int? RemainingMin,
    bool Monitoring,
    string? AlertMessage,
    string? DeviceId,
    DateTime? LastUpdated,
    string? PatientName,
    string? PatientCode,
    DateTime? AdmittedAt)
{
    public static BedDto From(BedState bed) => new(
        bed.BedId,
        bed.Room,
        bed.Status.ToString(),
        bed.Spo2,
        bed.HeartRate,
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
        bed.TsReady,
        bed.TsAnomaly,
        bed.TsEarlyWarning,
        bed.TsTrend,
        bed.HrForecast16s,
        bed.Spo2Forecast16s,
        bed.HrTrendBpmPerMin,
        bed.TsAnomalyScoreX100,
        bed.DropsTrend,
        bed.DropsTrendDpmPerMin,
        bed.DropsForecast16s,
        bed.HrForecastTrusted,
        bed.DropsForecastTrusted,
        bed.AlertLevel,
        bed.LineBranch,
        bed.PatientBranch,
        bed.DripAnomaly,
        bed.VitalsAnomaly,
        bed.LineState,
        bed.RemainingMl,
        bed.RemainingMin,
        bed.Monitoring,
        bed.AlertMessage,
        bed.DeviceId,
        bed.LastDataAt,
        bed.PatientName,
        bed.PatientCode,
        bed.AdmittedAt);
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
    DateTime? AcknowledgedAt,
    string? AcknowledgedBy,
    string? AcknowledgementNote)
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
        alert.AcknowledgedAt,
        alert.AcknowledgedBy,
        alert.AcknowledgementNote);
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
    DateTime? LastSeenAt,
    int? LinkQuality,
    string? ChannelsLost,
    DateTime? LastDataAt)
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
        device.LastSeenAt,
        device.LinkQuality,
        device.ChannelsLost,
        device.LastDataAt);
}

public sealed record DeviceEventDto(long EventId, string DeviceId, string? BedId,
                                    string EventType, string? Detail, DateTime OccurredAt)
{
    public static DeviceEventDto From(DeviceEvent e) => new(
        e.EventId, e.DeviceId, e.BedId,
        e.EventType.ToString().ToUpperInvariant(), e.Detail, e.OccurredAt);
}

public sealed record FaultReportDto(long ReportId, string BedId, string? DeviceId,
                                    string Channel, string Note, string ReportedBy,
                                    DateTime ReportedAt, string Status,
                                    string? HandledBy, DateTime? HandledAt,
                                    string? ResolutionNote)
{
    public static FaultReportDto From(FaultReport r) => new(
        r.ReportId, r.BedId, r.DeviceId,
        r.Channel.ToString().ToUpperInvariant(), r.Note, r.ReportedBy, r.ReportedAt,
        // IN_PROGRESS, not InProgress: the browser compares against these names.
        r.Status switch
        {
            FaultStatus.InProgress => "IN_PROGRESS",
            FaultStatus.Resolved => "RESOLVED",
            _ => "OPEN"
        },
        r.HandledBy, r.HandledAt, r.ResolutionNote);
}

public sealed record LogEntryDto(string BedId, string? Room, string Category, string Level, string Message, DateTime OccurredAt)
{
    public static LogEntryDto From(LogEntry entry) => new(
        entry.BedId, entry.Room, entry.Category.ToString(), entry.Level, entry.Message, entry.OccurredAt);
}


/// <summary>What the technician page needs to show a firmware update.</summary>
public sealed record OtaStatusDto(
    string DeviceId,
    string State,
    int? Progress,
    int? RemainingSeconds,
    string? Message,
    bool InFlight,
    DateTime UpdatedAt)
{
    public static OtaStatusDto From(OtaStatus s) => new(
        s.DeviceId, s.State.ToString(), s.Progress, s.RemainingSeconds,
        string.IsNullOrWhiteSpace(s.Message) ? null : s.Message,
        s.InFlight, s.UpdatedAt);
}
