using Dapper;
using HisServer.Domain;
using HisServer.Models;

namespace HisServer.Data;

public sealed class VitalSampleRepository
{
    private readonly DbConnectionFactory connectionFactory;

    public VitalSampleRepository(DbConnectionFactory connectionFactory)
    {
        this.connectionFactory = connectionFactory;
    }

    /// <summary>
    /// Appends one row to the vitals history. Takes the whole reading rather
    /// than a long positional parameter list, so adding a channel later is a
    /// one-line change here instead of a new argument threaded through the
    /// ingestion service.
    /// </summary>
    /// <remarks>
    /// Channels whose signal flag is false are stored as NULL, NOT as the 0
    /// the parser defaults them to. This matters for the charts: a gap says
    /// "sensor detached / not installed", whereas a plotted 0 would read as a
    /// patient with no pulse. Same reason the doctor's targets are captured on
    /// every row - drip_rate/flow_rate are ratios, so a historical "50%" is
    /// only interpretable next to the target that was in force at the time.
    /// </remarks>
    public async Task SaveAsync(
        BedReading reading,
        string? deviceId,
        BedStatus status,
        CancellationToken cancellationToken = default)
    {
        await using var connection = await connectionFactory.OpenConnectionAsync(cancellationToken);
        await connection.ExecuteAsync(
            """
            INSERT INTO vital_samples
              (bed_id, device_id, spo2, heart_rate, drip_rate, flow_rate,
               weight_g, drops_per_min, target_flow_ml_h, target_drops_per_min,
               line_blocked, ae_alarm, status, recorded_at)
            VALUES
              (@bedId, @deviceId, @spo2, @heartRate, @dripRate, @flowRate,
               @weightG, @dropsPerMin, @targetFlowMlH, @targetDropsPerMin,
               @lineBlocked, @aeAlarm, @status, @recordedAt)
            """,
            new
            {
                bedId = reading.BedId,
                deviceId,
                spo2 = reading.Spo2Signal ? reading.Spo2 : (int?)null,
                heartRate = reading.HeartRateSignal ? reading.HeartRate : (int?)null,
                dripRate = reading.DripRateSignal ? reading.DripRate : (int?)null,
                flowRate = reading.FlowSignal ? reading.FlowRate : (int?)null,
                weightG = reading.WeightG,
                dropsPerMin = reading.DropsPerMin,
                targetFlowMlH = reading.TargetFlowMlH,
                targetDropsPerMin = reading.TargetDropsPerMin,
                lineBlocked = reading.LineBlocked,
                aeAlarm = reading.AeAlarm,
                status = status.ToString().ToUpperInvariant(),
                recordedAt = reading.ReceivedAt
            });
    }

    /// <summary>
    /// Reads a bed's history for charting, newest-last.
    /// </summary>
    /// <remarks>
    /// The LIMIT is applied to the NEWEST rows (ORDER BY ... DESC) and the
    /// result reversed afterwards. Ordering ascending with a LIMIT would keep
    /// the OLDEST rows of the window instead, so a long range would chart
    /// ancient history and silently omit the present - the opposite of what a
    /// bedside view needs.
    /// </remarks>
    public async Task<IReadOnlyList<VitalSampleDto>> GetHistoryAsync(
        string bedId,
        DateTime from,
        DateTime to,
        int limit,
        CancellationToken cancellationToken = default)
    {
        await using var connection = await connectionFactory.OpenConnectionAsync(cancellationToken);
        var rows = await connection.QueryAsync<VitalSampleDto>(
            """
            SELECT
              recorded_at          AS RecordedAt,
              spo2                 AS Spo2,
              heart_rate           AS HeartRate,
              drip_rate            AS DripRate,
              flow_rate            AS FlowRate,
              weight_g             AS WeightG,
              drops_per_min        AS DropsPerMin,
              target_flow_ml_h     AS TargetFlowMlH,
              target_drops_per_min AS TargetDropsPerMin,
              line_blocked         AS LineBlocked,
              ae_alarm             AS AeAlarm,
              status               AS Status
            FROM vital_samples
            WHERE bed_id = @bedId
              AND recorded_at >= @from
              AND recorded_at <= @to
            ORDER BY recorded_at DESC
            LIMIT @limit
            """,
            new { bedId, from, to, limit });

        /* MySQL tra ve DATETIME khong kem thong tin mui gio, nen Dapper dung
         * DateTimeKind.Unspecified -> System.Text.Json serialize thanh
         * "2026-08-02T06:40:43.039" KHONG co hau to 'Z'. Trinh duyet doc chuoi
         * do la GIO DIA PHUONG, trong khi gia tri thuc su la UTC -> bieu do
         * hien lech dung bang chenh lech mui gio (o Viet Nam la 7 tieng).
         *
         * Cot nay LUON duoc ghi bang DateTime.UtcNow (xem BedDataParser), nen
         * danh dau Kind = Utc la dung ban chat du lieu, va sau khi danh dau thi
         * chuoi JSON co 'Z' va trinh duyet tu doi ve gio dia phuong. */
        return rows
            .Select(r => r with
            {
                RecordedAt = DateTime.SpecifyKind(r.RecordedAt, DateTimeKind.Utc)
            })
            .Reverse()
            .ToList();
    }
}
