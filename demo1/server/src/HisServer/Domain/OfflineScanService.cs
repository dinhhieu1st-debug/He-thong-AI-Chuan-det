using HisServer.Data;
using HisServer.Hubs;
using HisServer.Models;
using Microsoft.AspNetCore.SignalR;
using Microsoft.Extensions.Options;

namespace HisServer.Domain;

public sealed class OfflineOptions
{
    public int ThresholdSeconds { get; set; } = 3;
    public int ScanIntervalSeconds { get; set; } = 1;
}

/// <summary>
/// Periodically flips any bed that hasn't sent data within the configured
/// threshold to Offline. Replaces the old WinForms app's 1-second UI timer
/// with a proper background service, and now persists the transition and
/// broadcasts it over SignalR instead of only updating an in-memory dictionary.
/// </summary>
public sealed class OfflineScanService : BackgroundService
{
    private readonly BedStateStore bedStateStore;
    private readonly BedRepository bedRepository;
    private readonly IHubContext<MonitoringHub, IMonitoringClient> hub;
    private readonly IOptionsMonitor<OfflineOptions> options;
    private readonly ILogger<OfflineScanService> logger;

    public OfflineScanService(
        BedStateStore bedStateStore,
        BedRepository bedRepository,
        IHubContext<MonitoringHub, IMonitoringClient> hub,
        IOptionsMonitor<OfflineOptions> options,
        ILogger<OfflineScanService> logger)
    {
        this.bedStateStore = bedStateStore;
        this.bedRepository = bedRepository;
        this.hub = hub;
        this.options = options;
        this.logger = logger;
    }

    protected override async Task ExecuteAsync(CancellationToken stoppingToken)
    {
        while (!stoppingToken.IsCancellationRequested)
        {
            var interval = TimeSpan.FromSeconds(Math.Max(1, options.CurrentValue.ScanIntervalSeconds));
            var threshold = TimeSpan.FromSeconds(Math.Max(1, options.CurrentValue.ThresholdSeconds));
            var now = DateTime.UtcNow;

            foreach (var bed in bedStateStore.GetAll())
            {
                if (bed.Status == BedStatus.Offline)
                {
                    continue;
                }

                if (bed.LastDataAt is null || now - bed.LastDataAt.Value > threshold)
                {
                    var updated = bedStateStore.Upsert(bed.BedId, state => state.Status = BedStatus.Offline);

                    try
                    {
                        await bedRepository.UpsertAsync(updated, stoppingToken);
                    }
                    catch (Exception ex)
                    {
                        logger.LogWarning(ex, "Failed to persist offline transition for bed {BedId}.", bed.BedId);
                    }

                    await hub.Clients.Group(MonitoringHub.WardGroup).BedUpdated(BedDto.From(updated));
                }
            }

            try
            {
                await Task.Delay(interval, stoppingToken);
            }
            catch (OperationCanceledException)
            {
                break;
            }
        }
    }
}
