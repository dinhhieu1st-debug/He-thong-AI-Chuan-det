using Dapper;
using HisServer.Domain;
using HisServer.Models;

namespace HisServer.Data;

public sealed class BedRepository
{
    private readonly DbConnectionFactory connectionFactory;

    public BedRepository(DbConnectionFactory connectionFactory)
    {
        this.connectionFactory = connectionFactory;
    }

    public async Task<IReadOnlyList<BedState>> LoadAllAsync(CancellationToken cancellationToken = default)
    {
        await using var connection = await connectionFactory.OpenConnectionAsync(cancellationToken);
        var rows = await connection.QueryAsync(
            "SELECT bed_id, room, status, spo2, heart_rate, temperature, drip_rate, flow_rate, " +
            "heart_rate_signal, spo2_signal, flow_signal, drip_rate_signal, line_blocked, ae_alarm, " +
            "weight_g, drops_per_min, target_flow_ml_h, target_drops_per_min, tare_in_progress, " +
            "tare_just_completed, hr_baseline_just_completed, hr_baseline_seconds_remaining, " +
            "hr_baseline_bpm, hr_baseline_captured_at, last_tare_completed_at, " +
            "last_seen_tare_event_count, last_seen_hr_baseline_event_count, " +
            "alert_message, device_id, last_data_at, " +
            "patient_name, patient_code, admitted_at " +
            "FROM beds");

        return rows.Select(row => new BedState
        {
            BedId = row.bed_id,
            Room = row.room ?? string.Empty,
            Status = Enum.Parse<BedStatus>((string)row.status, ignoreCase: true),
            Spo2 = row.spo2,
            HeartRate = row.heart_rate,
            Temperature = (double?)row.temperature,
            DripRate = row.drip_rate,
            FlowRate = row.flow_rate,
            HeartRateSignal = row.heart_rate_signal,
            Spo2Signal = row.spo2_signal,
            FlowSignal = row.flow_signal,
            DripRateSignal = row.drip_rate_signal,
            LineBlocked = row.line_blocked,
            AeAlarm = row.ae_alarm,
            WeightG = row.weight_g,
            DropsPerMin = row.drops_per_min,
            TargetFlowMlH = row.target_flow_ml_h,
            TargetDropsPerMin = row.target_drops_per_min,
            TareInProgress = row.tare_in_progress,
            TareJustCompleted = row.tare_just_completed,
            HrBaselineJustCompleted = row.hr_baseline_just_completed,
            HrBaselineSecondsRemaining = row.hr_baseline_seconds_remaining,
            HrBaselineBpm = row.hr_baseline_bpm,
            HrBaselineCapturedAt = row.hr_baseline_captured_at,
            LastTareCompletedAt = row.last_tare_completed_at,
            LastSeenTareEventCount = row.last_seen_tare_event_count,
            LastSeenHrBaselineEventCount = row.last_seen_hr_baseline_event_count,
            AlertMessage = row.alert_message,
            DeviceId = row.device_id,
            LastDataAt = row.last_data_at,
            PatientName = row.patient_name,
            PatientCode = row.patient_code,
            AdmittedAt = row.admitted_at
        }).ToList();
    }

    /// <summary>
    /// Writes ONLY the patient columns.
    ///
    /// Deliberately not part of UpsertAsync: that runs on every reading from
    /// the device - once a second per bed - and knows nothing about patients,
    /// so folding these columns into it would blank the patient's name on the
    /// next packet. The same shape of bug has already happened here once, with
    /// the signal flags being reset on restart, and it is invisible until
    /// someone notices the name is gone.
    /// </summary>
    public async Task UpdatePatientAsync(string bedId, string? patientName, string? patientCode,
                                         DateTime? admittedAt, CancellationToken cancellationToken = default)
    {
        await using var connection = await connectionFactory.OpenConnectionAsync(cancellationToken);
        await connection.ExecuteAsync(
            """
            UPDATE beds
               SET patient_name = @patientName,
                   patient_code = @patientCode,
                   admitted_at  = @admittedAt
             WHERE bed_id = @bedId
            """,
            new { bedId, patientName, patientCode, admittedAt });
    }

    /// <summary>
    /// Removes a bed from the ward directory.
    ///
    /// Vital samples and alerts recorded against it are deliberately NOT
    /// deleted: they are the clinical record of what happened to a patient in
    /// that bed, and a bed being retired does not unmake any of it. Only the
    /// bed's own row goes.
    /// </summary>
    public async Task DeleteAsync(string bedId, CancellationToken cancellationToken = default)
    {
        await using var connection = await connectionFactory.OpenConnectionAsync(cancellationToken);
        await connection.ExecuteAsync("DELETE FROM beds WHERE bed_id = @bedId", new { bedId });
    }

    public async Task UpsertAsync(BedState bed, CancellationToken cancellationToken = default)
    {
        await using var connection = await connectionFactory.OpenConnectionAsync(cancellationToken);
        await connection.ExecuteAsync(
            """
            INSERT INTO beds (bed_id, room, status, spo2, heart_rate, temperature, drip_rate, flow_rate,
              heart_rate_signal, spo2_signal, flow_signal, drip_rate_signal, line_blocked, ae_alarm,
              weight_g, drops_per_min, target_flow_ml_h, target_drops_per_min, tare_in_progress,
              tare_just_completed, hr_baseline_just_completed, hr_baseline_seconds_remaining,
              hr_baseline_bpm, hr_baseline_captured_at, last_tare_completed_at,
              last_seen_tare_event_count, last_seen_hr_baseline_event_count,
              alert_message, device_id, last_data_at)
            VALUES (@BedId, @Room, @Status, @Spo2, @HeartRate, @Temperature, @DripRate, @FlowRate,
              @HeartRateSignal, @Spo2Signal, @FlowSignal, @DripRateSignal, @LineBlocked, @AeAlarm,
              @WeightG, @DropsPerMin, @TargetFlowMlH, @TargetDropsPerMin, @TareInProgress, @TareJustCompleted,
              @HrBaselineJustCompleted, @HrBaselineSecondsRemaining, @HrBaselineBpm, @HrBaselineCapturedAt,
              @LastTareCompletedAt, @LastSeenTareEventCount, @LastSeenHrBaselineEventCount,
              @AlertMessage, @DeviceId, @LastDataAt)
            ON DUPLICATE KEY UPDATE
              room = VALUES(room),
              status = VALUES(status),
              spo2 = VALUES(spo2),
              heart_rate = VALUES(heart_rate),
              temperature = VALUES(temperature),
              drip_rate = VALUES(drip_rate),
              flow_rate = VALUES(flow_rate),
              heart_rate_signal = VALUES(heart_rate_signal),
              spo2_signal = VALUES(spo2_signal),
              flow_signal = VALUES(flow_signal),
              drip_rate_signal = VALUES(drip_rate_signal),
              line_blocked = VALUES(line_blocked),
              ae_alarm = VALUES(ae_alarm),
              weight_g = VALUES(weight_g),
              drops_per_min = VALUES(drops_per_min),
              target_flow_ml_h = VALUES(target_flow_ml_h),
              target_drops_per_min = VALUES(target_drops_per_min),
              tare_in_progress = VALUES(tare_in_progress),
              tare_just_completed = VALUES(tare_just_completed),
              hr_baseline_just_completed = VALUES(hr_baseline_just_completed),
              hr_baseline_seconds_remaining = VALUES(hr_baseline_seconds_remaining),
              hr_baseline_bpm = VALUES(hr_baseline_bpm),
              hr_baseline_captured_at = VALUES(hr_baseline_captured_at),
              last_tare_completed_at = VALUES(last_tare_completed_at),
              last_seen_tare_event_count = VALUES(last_seen_tare_event_count),
              last_seen_hr_baseline_event_count = VALUES(last_seen_hr_baseline_event_count),
              alert_message = VALUES(alert_message),
              device_id = VALUES(device_id),
              last_data_at = VALUES(last_data_at)
            """,
            new
            {
                bed.BedId,
                bed.Room,
                Status = bed.Status.ToString().ToUpperInvariant(),
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
                bed.LastSeenTareEventCount,
                bed.LastSeenHrBaselineEventCount,
                bed.AlertMessage,
                bed.DeviceId,
                bed.LastDataAt
            });
    }
}
