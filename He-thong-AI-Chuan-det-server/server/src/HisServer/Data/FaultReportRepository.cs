using Dapper;
using HisServer.Models;

namespace HisServer.Data;

/// <summary>
/// Nurse-raised "this equipment is broken" reports, and the technician's work
/// on them.
/// </summary>
public sealed class FaultReportRepository
{
    private readonly DbConnectionFactory connectionFactory;

    public FaultReportRepository(DbConnectionFactory connectionFactory)
    {
        this.connectionFactory = connectionFactory;
    }

    private const string Columns =
        "report_id, bed_id, device_id, channel, note, reported_by, reported_at, " +
        "status, handled_by, handled_at, resolution_note";

    private static FaultReport MapRow(dynamic row) => new()
    {
        ReportId = (long)row.report_id,
        BedId = (string)row.bed_id,
        DeviceId = row.device_id,
        Channel = Enum.Parse<FaultChannel>((string)row.channel, ignoreCase: true),
        Note = (string)row.note,
        ReportedBy = (string)row.reported_by,
        ReportedAt = (DateTime)row.reported_at,
        // IN_PROGRESS in the database, InProgress in C#.
        Status = Enum.Parse<FaultStatus>(((string)row.status).Replace("_", ""), ignoreCase: true),
        HandledBy = row.handled_by,
        HandledAt = row.handled_at,
        ResolutionNote = row.resolution_note
    };

    private static string ToDbStatus(FaultStatus status) => status switch
    {
        FaultStatus.InProgress => "IN_PROGRESS",
        _ => status.ToString().ToUpperInvariant()
    };

    public async Task<long> CreateAsync(string bedId, string? deviceId, FaultChannel channel,
                                        string note, string reportedBy, CancellationToken ct = default)
    {
        await using var connection = await connectionFactory.OpenConnectionAsync(ct);
        return await connection.ExecuteScalarAsync<long>(
            """
            INSERT INTO device_fault_reports (bed_id, device_id, channel, note, reported_by)
            VALUES (@bedId, @deviceId, @channel, @note, @reportedBy);
            SELECT LAST_INSERT_ID();
            """,
            new
            {
                bedId,
                deviceId,
                channel = channel.ToString().ToUpperInvariant(),
                note,
                reportedBy
            });
    }

    public async Task<FaultReport?> GetAsync(long reportId, CancellationToken ct = default)
    {
        await using var connection = await connectionFactory.OpenConnectionAsync(ct);
        var row = await connection.QuerySingleOrDefaultAsync(
            $"SELECT {Columns} FROM device_fault_reports WHERE report_id = @reportId",
            new { reportId });
        return row is null ? null : MapRow(row);
    }

    /// <summary>
    /// The technician's queue. Open work first, then in progress, then the
    /// recently resolved - a queue that hides what was just closed makes it
    /// impossible to tell "nobody has looked at this" from "someone fixed it
    /// two minutes ago".
    /// </summary>
    public async Task<IReadOnlyList<FaultReport>> GetQueueAsync(bool openOnly, int limit = 100,
                                                                CancellationToken ct = default)
    {
        await using var connection = await connectionFactory.OpenConnectionAsync(ct);
        var where = openOnly ? "WHERE status <> 'RESOLVED'" : string.Empty;
        var rows = await connection.QueryAsync(
            $"""
             SELECT {Columns} FROM device_fault_reports
             {where}
             ORDER BY FIELD(status, 'OPEN', 'IN_PROGRESS', 'RESOLVED'), reported_at DESC
             LIMIT @limit
             """,
            new { limit });
        return rows.Select(r => (FaultReport)MapRow(r)).ToList();
    }

    /// <summary>Reports for one bed, so the nurse who raised one can see
    /// whether anybody picked it up.</summary>
    public async Task<IReadOnlyList<FaultReport>> GetForBedAsync(string bedId, int limit = 20,
                                                                CancellationToken ct = default)
    {
        await using var connection = await connectionFactory.OpenConnectionAsync(ct);
        var rows = await connection.QueryAsync(
            $"SELECT {Columns} FROM device_fault_reports WHERE bed_id = @bedId " +
            "ORDER BY reported_at DESC LIMIT @limit",
            new { bedId, limit });
        return rows.Select(r => (FaultReport)MapRow(r)).ToList();
    }

    public async Task UpdateStatusAsync(long reportId, FaultStatus status, string handledBy,
                                        string? resolutionNote, CancellationToken ct = default)
    {
        await using var connection = await connectionFactory.OpenConnectionAsync(ct);
        await connection.ExecuteAsync(
            """
            UPDATE device_fault_reports
               SET status = @status,
                   handled_by = @handledBy,
                   handled_at = UTC_TIMESTAMP(3),
                   resolution_note = COALESCE(@resolutionNote, resolution_note)
             WHERE report_id = @reportId
            """,
            new { reportId, status = ToDbStatus(status), handledBy, resolutionNote });
    }

    public async Task<int> CountOpenAsync(CancellationToken ct = default)
    {
        await using var connection = await connectionFactory.OpenConnectionAsync(ct);
        return await connection.ExecuteScalarAsync<int>(
            "SELECT COUNT(*) FROM device_fault_reports WHERE status <> 'RESOLVED'");
    }
}
