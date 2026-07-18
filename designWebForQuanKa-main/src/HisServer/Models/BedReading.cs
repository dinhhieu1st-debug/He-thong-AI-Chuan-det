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
    bool AeAlarm = false);
