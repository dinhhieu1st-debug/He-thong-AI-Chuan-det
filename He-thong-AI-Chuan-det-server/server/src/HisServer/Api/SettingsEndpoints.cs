using HisServer.Domain;
using HisServer.Services;
using Microsoft.Extensions.Options;

namespace HisServer.Api;

public static class SettingsEndpoints
{
    public static void MapSettingsEndpoints(this IEndpointRouteBuilder app)
    {
        app.MapGet("/api/settings", (
            IOptionsMonitor<VitalsSaveOptions> vitalsSave,
            IOptionsMonitor<OfflineOptions> offline) =>
        {
            return Results.Ok(new
            {
                vitalsSaveIntervalSeconds = vitalsSave.CurrentValue.IntervalSeconds,
                offlineThresholdSeconds = offline.CurrentValue.ThresholdSeconds
            });
        });
    }
}
