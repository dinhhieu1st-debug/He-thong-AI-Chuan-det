using System.Security.Cryptography;
using System.Text;
using System.Text.Json;
using HisServer.Domain;
using HisServer.Ingestion;
using Microsoft.Extensions.Options;

namespace HisServer.Api;

public sealed class HttpIngestionOptions
{
    public string ApiKey { get; set; } = string.Empty;
}

public static class IngestionEndpoints
{
    private const long MaximumPayloadBytes = 64 * 1024;

    public static void MapIngestionEndpoints(this IEndpointRouteBuilder app)
    {
        app.MapPost("/api/ingestion/bed", async (
            HttpRequest request,
            BedTcpIngestionService ingestion,
            BedConnectionRegistry connections,
            IOptionsMonitor<HttpIngestionOptions> options,
            CancellationToken cancellationToken) =>
        {
            var expectedKey = options.CurrentValue.ApiKey;
            if (string.IsNullOrWhiteSpace(expectedKey))
            {
                return Results.Json(
                    new { error = "HTTP ingestion is not configured" },
                    statusCode: StatusCodes.Status503ServiceUnavailable);
            }

            var suppliedKey = request.Headers["X-Ingestion-Key"].ToString();
            if (!KeysMatch(expectedKey, suppliedKey))
            {
                return Results.Unauthorized();
            }

            var gatewayId = request.Headers["X-Gateway-Id"].ToString().Trim();
            if (gatewayId.Length is 0 or > 128)
            {
                return Results.BadRequest(new { error = "Missing or invalid X-Gateway-Id" });
            }

            if (request.ContentLength is > MaximumPayloadBytes)
            {
                return Results.StatusCode(StatusCodes.Status413PayloadTooLarge);
            }

            JsonDocument document;
            try
            {
                document = await JsonDocument.ParseAsync(
                    request.Body,
                    new JsonDocumentOptions { MaxDepth = 32 },
                    cancellationToken);
            }
            catch (JsonException)
            {
                return Results.BadRequest(new { error = "Invalid gateway JSON" });
            }

            using (document)
            {
                var bedId = await ingestion.IngestReadingLineAsync(
                    document.RootElement.GetRawText(), cancellationToken);
                if (bedId is not null)
                {
                    connections.RegisterHttp(bedId, gatewayId);
                }
            }

            var commands = connections.DrainHttpCommands(gatewayId);
            return commands.Count == 0
                ? Results.Accepted()
                : Results.Text(string.Join('\n', commands) + "\n", "text/plain");
        }).AllowAnonymous();
    }

    private static bool KeysMatch(string expected, string supplied)
    {
        var expectedHash = SHA256.HashData(Encoding.UTF8.GetBytes(expected));
        var suppliedHash = SHA256.HashData(Encoding.UTF8.GetBytes(supplied));
        return CryptographicOperations.FixedTimeEquals(expectedHash, suppliedHash);
    }
}
