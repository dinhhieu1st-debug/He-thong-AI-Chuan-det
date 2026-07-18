namespace HisServer.Models;

/// <summary>Raw vitals reading as received over the TCP ingestion socket.</summary>
public sealed record BedReading(
    string BedId,
    string Room,
    int Spo2,
    int HeartRate,
    double Temperature,
    int DripRate,
    DateTime ReceivedAt,
    int FlowRate = 0,
    bool HeartRateSignal = true,
    bool Spo2Signal = true,
    bool FlowSignal = true,
    bool DripRateSignal = true,
    bool LineBlocked = false,
    bool AeAlarm = false,
    int? WeightG = null,
    int? DropsPerMin = null,
    int? TargetFlowMlH = null,
    int? TargetDropsPerMin = null,
    bool TareInProgress = false,
    bool TareJustCompleted = false,
    bool HrBaselineJustCompleted = false,
    int? HrBaselineSecondsRemaining = null,
    int? HrBaselineBpm = null,
    int? TareEventCount = null,
    int? HrBaselineEventCount = null);
