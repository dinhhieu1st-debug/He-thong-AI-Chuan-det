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
    public int? WeightG { get; set; }
    public int? DropsPerMin { get; set; }
    public int? TargetFlowMlH { get; set; }
    public int? TargetDropsPerMin { get; set; }
    public bool TareInProgress { get; set; }
    public bool TareJustCompleted { get; set; }
    public bool HrBaselineJustCompleted { get; set; }
    public int? HrBaselineSecondsRemaining { get; set; }
    public int? HrBaselineBpm { get; set; }

    /// <summary>
    /// Ket qua model du bao chuoi thoi gian chay TREN CHIP (xem ts_monitor.c).
    /// TsTrend: 0 = on dinh, 1 = nhip tim dang tang, 2 = dang giam.
    /// TsAnomalyScoreX100 da nhan 100 (firmware gui so nguyen).
    /// </summary>
    public bool TsReady { get; set; }
    public bool TsAnomaly { get; set; }
    public bool TsEarlyWarning { get; set; }
    public int? TsTrend { get; set; }
    public int? HrForecast16s { get; set; }
    public int? Spo2Forecast16s { get; set; }
    public int? HrTrendBpmPerMin { get; set; }
    public int? TsAnomalyScoreX100 { get; set; }

    /// <summary>
    /// Xu huong toc do giot - chi dau quan trong nhat voi may truyen dich:
    /// giot cham dan bao hieu tac duong truyen SOM hon la doi ti le giot tut
    /// qua nguong bao dong. DropsTrend: 0 = on dinh, 1 = tang, 2 = giam.
    /// </summary>
    public int? DropsTrend { get; set; }
    public int? DropsTrendDpmPerMin { get; set; }
    public int? DropsForecast16s { get; set; }

    /// <summary>
    /// Con so du bao cua kenh do co doc duoc nhu "du bao" khong. Khi false, no
    /// la "muc binh thuong ky vong" chu khong phai du doan tuong lai - giao dien
    /// phai doi nhan, neu khong bac si se hieu nham hoan toan.
    /// </summary>
    public bool HrForecastTrusted { get; set; } = true;
    public bool DropsForecastTrusted { get; set; } = true;

    /// <summary>
    /// Wall-clock timestamps stamped by THIS server (not the firmware, which
    /// has no wall-clock time - only relative uptime) the moment it observes
    /// the corresponding one-shot "just completed" flag go true. Lets the UI
    /// show "Baseline captured at HH:MM:SS" / "Last tared at HH:MM:SS"
    /// persistently, instead of relying on catching a transient toast.
    /// </summary>
    public DateTime? HrBaselineCapturedAt { get; set; }
    public DateTime? LastTareCompletedAt { get; set; }

    /// <summary>
    /// The last TareEventCount/HrBaselineEventCount value seen from this bed -
    /// internal bookkeeping only (not exposed via BedDto). The firmware's
    /// one-shot completion flags above can be MISSED if the event fires
    /// before Zigbee reporting is configured (the auto-tare at boot is fast
    /// enough to race with network join). These persistent counters can't be
    /// missed: BedTcpIngestionService compares each incoming reading's count
    /// against what's stored here and stamps a fresh timestamp whenever it
    /// differs, regardless of exactly when the underlying event happened.
    /// </summary>
    public int? LastSeenTareEventCount { get; set; }
    public int? LastSeenHrBaselineEventCount { get; set; }

    public string? AlertMessage { get; set; }
    public string? DeviceId { get; set; }
    public DateTime? LastDataAt { get; set; }

    /* Who is in the bed. Kept on the bed rather than in a patients table
     * because a bed holds at most one patient at a time and nothing here needs
     * a patient's history - the moment it does, this moves out. Set by a nurse
     * on admission and cleared on discharge; null means the bed is empty. */
    public string? PatientName { get; set; }
    public string? PatientCode { get; set; }
    public DateTime? AdmittedAt { get; set; }
}
