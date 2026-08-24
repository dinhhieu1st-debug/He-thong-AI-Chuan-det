using System.Text;
using Dapper;
using HisServer.Models;

namespace HisServer.Data;

public sealed class LogQuery
{
    public DateTime? From { get; init; }
    public DateTime? To { get; init; }
    public string? BedId { get; init; }
    public string? Room { get; init; }
    public LogCategory? Category { get; init; }
    public int Page { get; init; } = 1;
    public int PageSize { get; init; } = 80;
}

public sealed class LogRepository
{
    private readonly DbConnectionFactory connectionFactory;

    public LogRepository(DbConnectionFactory connectionFactory)
    {
        this.connectionFactory = connectionFactory;
    }

    public async Task<(IReadOnlyList<LogEntry> Items, int TotalCount)> QueryAsync(
        LogQuery query, CancellationToken cancellationToken = default)
    {
        await using var connection = await connectionFactory.OpenConnectionAsync(cancellationToken);

        var (sql, parameters) = BuildUnionSql(query, paginate: true);
        var rows = await connection.QueryAsync(sql, parameters);
        var items = rows.Select(MapRow).ToList();

        var (countSql, countParameters) = BuildUnionSql(query, paginate: false, countOnly: true);
        var totalCount = await connection.ExecuteScalarAsync<int>(countSql, countParameters);

        return (items, totalCount);
    }

    /// <summary>Streams all rows matching the filter (no pagination cap) for CSV export.</summary>
    public async Task<IReadOnlyList<LogEntry>> QueryAllAsync(
        LogQuery query, CancellationToken cancellationToken = default)
    {
        await using var connection = await connectionFactory.OpenConnectionAsync(cancellationToken);
        var (sql, parameters) = BuildUnionSql(query, paginate: false);
        var rows = await connection.QueryAsync(sql, parameters);
        return rows.Select(MapRow).ToList();
    }

    private static (string Sql, DynamicParameters Parameters) BuildUnionSql(
        LogQuery query, bool paginate, bool countOnly = false)
    {
        var parameters = new DynamicParameters();
        var alertWhere = new StringBuilder(" WHERE 1=1");
        var vitalWhere = new StringBuilder(" WHERE 1=1");

        if (query.From is not null)
        {
            alertWhere.Append(" AND a.created_at >= @from");
            vitalWhere.Append(" AND v.recorded_at >= @from");
            parameters.Add("from", query.From);
        }

        if (query.To is not null)
        {
            alertWhere.Append(" AND a.created_at <= @to");
            vitalWhere.Append(" AND v.recorded_at <= @to");
            parameters.Add("to", query.To);
        }

        if (!string.IsNullOrWhiteSpace(query.BedId))
        {
            alertWhere.Append(" AND a.bed_id = @bedId");
            vitalWhere.Append(" AND v.bed_id = @bedId");
            parameters.Add("bedId", query.BedId);
        }

        if (!string.IsNullOrWhiteSpace(query.Room))
        {
            alertWhere.Append(" AND b1.room = @room");
            vitalWhere.Append(" AND b2.room = @room");
            parameters.Add("room", query.Room);
        }

        var includeAlerts = query.Category is null or LogCategory.Alert;
        var includeVitals = query.Category is null or LogCategory.Vital;

        var parts = new List<string>();
        if (includeAlerts)
        {
            parts.Add(
                """
                SELECT a.bed_id AS bed_id, b1.room AS room, 'ALERT' AS category,
                       a.alert_level AS level, a.message AS message, a.created_at AS occurred_at
                FROM alerts a
                LEFT JOIN beds b1 ON b1.bed_id = a.bed_id
                """ + alertWhere);
        }

        if (includeVitals)
        {
            parts.Add(
                """
                SELECT v.bed_id AS bed_id, b2.room AS room, 'VITAL' AS category,
                       v.status AS level,
                       CONCAT('SpO2: ', COALESCE(v.spo2, '--'), '%, HR: ', COALESCE(v.heart_rate, '--'),
                              ' bpm, Drip: ', COALESCE(v.drip_rate, '--'), '%') AS message,
                       v.recorded_at AS occurred_at
                FROM vital_samples v
                LEFT JOIN beds b2 ON b2.bed_id = v.bed_id
                """ + vitalWhere);
        }

        var union = string.Join(" UNION ALL ", parts);

        if (countOnly)
        {
            return ($"SELECT COUNT(*) FROM ({union}) merged", parameters);
        }

        var sql = $"SELECT * FROM ({union}) merged ORDER BY occurred_at DESC";

        if (paginate)
        {
            sql += " LIMIT @pageSize OFFSET @offset";
            parameters.Add("pageSize", query.PageSize);
            parameters.Add("offset", (query.Page - 1) * query.PageSize);
        }

        return (sql, parameters);
    }

    private static LogEntry MapRow(dynamic row) => new()
    {
        BedId = row.bed_id,
        Room = row.room,
        Category = ((string)row.category) == "ALERT" ? LogCategory.Alert : LogCategory.Vital,
        Level = row.level,
        Message = row.message,
        OccurredAt = row.occurred_at
    };
}
