using System.Text;
using Dapper;
using HisServer.Domain;
using HisServer.Models;

namespace HisServer.Data;

public sealed class AlertQuery
{
    public bool? Acknowledged { get; init; }
    public BedStatus? Level { get; init; }
    public int Page { get; init; } = 1;
    public int PageSize { get; init; } = 80;
}

public sealed class AlertRepository
{
    private readonly DbConnectionFactory connectionFactory;

    public AlertRepository(DbConnectionFactory connectionFactory)
    {
        this.connectionFactory = connectionFactory;
    }

    public async Task<long> InsertAsync(AlertRecord alert, CancellationToken cancellationToken = default)
    {
        await using var connection = await connectionFactory.OpenConnectionAsync(cancellationToken);
        var id = await connection.ExecuteScalarAsync<long>(
            """
            INSERT INTO alerts (bed_id, device_id, alert_level, alert_type, message, spo2, heart_rate, temperature, drip_rate, created_at)
            VALUES (@BedId, @DeviceId, @Level, @AlertType, @Message, @Spo2, @HeartRate, @Temperature, @DripRate, @CreatedAt);
            SELECT LAST_INSERT_ID();
            """,
            new
            {
                alert.BedId,
                alert.DeviceId,
                Level = alert.Level.ToString().ToUpperInvariant(),
                alert.AlertType,
                alert.Message,
                alert.Spo2,
                alert.HeartRate,
                alert.Temperature,
                alert.DripRate,
                alert.CreatedAt
            });
        return id;
    }

    public async Task<(IReadOnlyList<AlertRecord> Items, int TotalCount)> QueryAsync(
        AlertQuery query, CancellationToken cancellationToken = default)
    {
        await using var connection = await connectionFactory.OpenConnectionAsync(cancellationToken);

        var where = new StringBuilder(" WHERE 1=1");
        var parameters = new DynamicParameters();

        if (query.Acknowledged is not null)
        {
            where.Append(" AND a.acknowledged = @acknowledged");
            parameters.Add("acknowledged", query.Acknowledged);
        }

        if (query.Level is not null)
        {
            where.Append(" AND a.alert_level = @level");
            parameters.Add("level", query.Level.Value.ToString().ToUpperInvariant());
        }

        var totalCount = await connection.ExecuteScalarAsync<int>(
            "SELECT COUNT(*) FROM alerts a" + where, parameters);

        parameters.Add("offset", (query.Page - 1) * query.PageSize);
        parameters.Add("pageSize", query.PageSize);

        var rows = await connection.QueryAsync(
            """
            SELECT a.alert_id, a.bed_id, b.room, a.device_id, a.alert_level, a.alert_type, a.message,
                   a.spo2, a.heart_rate, a.temperature, a.drip_rate, a.acknowledged, a.acknowledged_at, a.created_at
            FROM alerts a
            LEFT JOIN beds b ON b.bed_id = a.bed_id
            """ + where + " " + """
            ORDER BY a.acknowledged ASC, (a.alert_level = 'CRITICAL') DESC, a.created_at DESC
            LIMIT @pageSize OFFSET @offset
            """,
            parameters);

        var items = rows.Select(MapRow).ToList();
        return (items, totalCount);
    }

    public async Task<AlertRecord?> GetAsync(long alertId, CancellationToken cancellationToken = default)
    {
        await using var connection = await connectionFactory.OpenConnectionAsync(cancellationToken);
        var row = await connection.QuerySingleOrDefaultAsync(
            """
            SELECT a.alert_id, a.bed_id, b.room, a.device_id, a.alert_level, a.alert_type, a.message,
                   a.spo2, a.heart_rate, a.temperature, a.drip_rate, a.acknowledged, a.acknowledged_at, a.created_at
            FROM alerts a
            LEFT JOIN beds b ON b.bed_id = a.bed_id
            WHERE a.alert_id = @alertId
            """,
            new { alertId });
        return row is null ? null : MapRow(row);
    }

    public async Task<DateTime?> AcknowledgeAsync(long alertId, CancellationToken cancellationToken = default)
    {
        await using var connection = await connectionFactory.OpenConnectionAsync(cancellationToken);
        await connection.ExecuteAsync(
            "UPDATE alerts SET acknowledged = TRUE, acknowledged_at = COALESCE(acknowledged_at, NOW()) WHERE alert_id = @alertId",
            new { alertId });

        var acknowledgedAt = await connection.ExecuteScalarAsync<DateTime?>(
            "SELECT acknowledged_at FROM alerts WHERE alert_id = @alertId", new { alertId });
        return acknowledgedAt;
    }

    private static AlertRecord MapRow(dynamic row) => new()
    {
        AlertId = row.alert_id,
        BedId = row.bed_id,
        Room = row.room,
        DeviceId = row.device_id,
        Level = Enum.Parse<BedStatus>((string)row.alert_level, ignoreCase: true),
        AlertType = row.alert_type,
        Message = row.message,
        Spo2 = row.spo2,
        HeartRate = row.heart_rate,
        Temperature = (double?)row.temperature,
        DripRate = row.drip_rate,
        Acknowledged = row.acknowledged,
        AcknowledgedAt = row.acknowledged_at,
        CreatedAt = row.created_at
    };
}
