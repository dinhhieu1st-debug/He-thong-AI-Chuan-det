namespace HisServer.Models;

/// <summary>Raw vitals reading as received over the TCP ingestion socket.</summary>
public sealed record BedReading(
    string BedId,
    string Room,
    int Spo2,
    int HeartRate,
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
    int? HrBaselineEventCount = null,
    bool TsReady = false,
    bool TsAnomaly = false,
    bool TsEarlyWarning = false,
    int? TsTrend = null,
    int? HrForecast16s = null,
    int? Spo2Forecast16s = null,
    int? HrTrendBpmPerMin = null,
    int? TsAnomalyScoreX100 = null,
    int? DropsTrend = null,
    int? DropsTrendDpmPerMin = null,
    int? DropsForecast16s = null,
    bool HrForecastTrusted = true,
    bool DropsForecastTrusted = true,
    /// <summary>Zigbee signal strength 0-255, or null from a gateway too old
    /// to send it. Equipment diagnostics only - never shown on a ward screen.</summary>
    int? LinkQuality = null,
    /// <summary>Which device sent this reading. Decides the bed the readings
    /// belong to - see the routing note in BedTcpIngestionService.</summary>
    string? DeviceId = null,

    // ---- AI v2 -----------------------------------------------------------
    // The device now runs three independent models and reports a four-level
    // alert plus WHICH SIDE is at fault. That distinction is the one a nurse
    // acts on first: a blocked line and a deteriorating patient need completely
    // different responses, and v1 gave them the same alarm.
    //
    // All nullable/defaulted, because a device still on v1 firmware sends none
    // of them and must keep working.

    /// <summary>0 normal, 1 infusion line, 2 patient vitals, 3 both at once.
    /// Null from a device too old to report it.</summary>
    int? AlertLevel = null,
    /// <summary>Canonical severity decided by the XG26: 1 normal, 2 warning,
    /// 3 critical. When present the server must not recalculate severity from
    /// thresholds or sensor availability.</summary>
    int? FinalAlertLevel = null,
    /// <summary>The infusion line is at fault: drip model anomaly, or the
    /// load-cell cross-check concluded occlusion / free flow.</summary>
    bool LineBranch = false,
    /// <summary>The patient is at fault: vitals model anomaly, vitals
    /// autoencoder anomaly, or a hard clinical limit breached.</summary>
    bool PatientBranch = false,
    /// <summary>Model 1 (drip forecaster), confirmed through the K=11 filter.</summary>
    bool DripAnomaly = false,
    /// <summary>Model 2 (vitals forecaster), confirmed through the K=11 filter.</summary>
    bool VitalsAnomaly = false,
    /// <summary>Load-cell verdict: 0 ok, 1 running low, 2 occlusion, 3 free
    /// flow, 4 drop-sensor fault, 5 empty. Null while the 60 s weight trend is
    /// still filling, which is NOT the same as "everything fine".</summary>
    int? LineState = null,
    /// <summary>Estimated fluid left, and how long at the current rate. Null
    /// when the load cell cannot estimate it.</summary>
    int? RemainingMl = null,
    int? RemainingMin = null,

    /// <summary>
    /// Thiết bị đang theo dõi, hay đang ở chế độ chờ (y tá chưa bấm bắt đầu).
    /// Mặc định true cho thiết bị đời cũ chưa báo trường này: một giường đang
    /// được giám sát thật mà bị hiện thành "chờ" là kiểu sai nguy hiểm hơn
    /// nhiều so với chiều ngược lại.
    /// </summary>
    bool Monitoring = true,
    int? DropTrainingSamples = null,
    int? VitalsTrainingSamples = null,
    bool? AlertsArmed = null);
