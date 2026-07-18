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
            "alert_message, device_id, last_data_at " +
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
            AlertMessage = row.alert_message,
            DeviceId = row.device_id,
            LastDataAt = row.last_data_at
        }).ToList();
    }

    public async Task UpsertAsync(BedState bed, CancellationToken cancellationToken = default)
    {
        await using var connection = await connectionFactory.OpenConnectionAsync(cancellationToken);
        await connection.ExecuteAsync(
            """
            INSERT INTO beds (bed_id, room, status, spo2, heart_rate, temperature, drip_rate, flow_rate,
              heart_rate_signal, spo2_signal, flow_signal, drip_rate_signal, line_blocked, ae_alarm,
              alert_message, device_id, last_data_at)
            VALUES (@BedId, @Room, @Status, @Spo2, @HeartRate, @Temperature, @DripRate, @FlowRate,
              @HeartRateSignal, @Spo2Signal, @FlowSignal, @DripRateSignal, @LineBlocked, @AeAlarm,
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
                bed.AlertMessage,
                bed.DeviceId,
                bed.LastDataAt
            });
    }
}
