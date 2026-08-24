using System.Collections.Concurrent;
using Microsoft.Extensions.Options;

namespace HisServer.Services;

public sealed class VitalsSaveOptions
{
    public int IntervalSeconds { get; set; } = 10;
}

/// <summary>
/// Throttles how often a given bed's vitals get persisted to `vital_samples` —
/// every ingestion tick still updates the live in-memory state, but only one
/// row per bed per configured interval is written to the database.
/// </summary>
public sealed class VitalsPersistenceCoordinator
{
    private readonly ConcurrentDictionary<string, DateTime> lastSavedAt = new(StringComparer.OrdinalIgnoreCase);
    private readonly IOptionsMonitor<VitalsSaveOptions> options;

    public VitalsPersistenceCoordinator(IOptionsMonitor<VitalsSaveOptions> options)
    {
        this.options = options;
    }

    public bool ShouldSave(string bedId, DateTime now)
    {
        var interval = TimeSpan.FromSeconds(Math.Max(1, options.CurrentValue.IntervalSeconds));

        if (lastSavedAt.TryGetValue(bedId, out var last) && now - last < interval)
        {
            return false;
        }

        lastSavedAt[bedId] = now;
        return true;
    }
}
