using System.Text;
using HisServer.Data;
using HisServer.Models;

namespace HisServer.Api;

public static class LogEndpoints
{
    public static void MapLogEndpoints(this IEndpointRouteBuilder app)
    {
        app.MapGet("/api/logs", async (
            LogRepository repository,
            DateTime? from,
            DateTime? to,
            string? bedId,
            string? room,
            string? type,
            int? page,
            int? pageSize) =>
        {
            var query = BuildQuery(from, to, bedId, room, type, page, pageSize);
            var (items, totalCount) = await repository.QueryAsync(query);
            return Results.Ok(new
            {
                items = items.Select(LogEntryDto.From),
                totalCount,
                page = query.Page,
                pageSize = query.PageSize
            });
        });

        app.MapGet("/api/logs/export", async (
            LogRepository repository,
            DateTime? from,
            DateTime? to,
            string? bedId,
            string? room,
            string? type) =>
        {
            var query = BuildQuery(from, to, bedId, room, type, page: 1, pageSize: 0);
            var entries = await repository.QueryAllAsync(query);

            var csv = new StringBuilder();
            csv.AppendLine("Time,Category,Bed,Room,Level,Message");
            foreach (var entry in entries)
            {
                csv.AppendLine(string.Join(',',
                    CsvField(entry.OccurredAt.ToString("yyyy-MM-dd HH:mm:ss")),
                    CsvField(entry.Category.ToString()),
                    CsvField(entry.BedId),
                    CsvField(entry.Room ?? string.Empty),
                    CsvField(entry.Level),
                    CsvField(entry.Message)));
            }

            var bytes = Encoding.UTF8.GetBytes(csv.ToString());
            return Results.File(bytes, "text/csv", $"system-log-{DateTime.UtcNow:yyyyMMdd-HHmmss}.csv");
        });
    }

    private static LogQuery BuildQuery(DateTime? from, DateTime? to, string? bedId, string? room, string? type, int? page, int? pageSize)
    {
        LogCategory? category = null;
        if (!string.IsNullOrWhiteSpace(type) && Enum.TryParse<LogCategory>(type, ignoreCase: true, out var parsed))
        {
            category = parsed;
        }

        return new LogQuery
        {
            From = from,
            To = to,
            BedId = string.IsNullOrWhiteSpace(bedId) ? null : bedId,
            Room = string.IsNullOrWhiteSpace(room) ? null : room,
            Category = category,
            Page = page is null or <= 0 ? 1 : page.Value,
            PageSize = pageSize is null or <= 0 ? 80 : pageSize.Value
        };
    }

    private static string CsvField(string value)
    {
        var escaped = value.Replace("\"", "\"\"");
        return $"\"{escaped}\"";
    }
}
