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
                    /* Going Offline must also drop the per-channel signal
                     * flags and any alarm verdict.
                     *
                     * A bed card shows a reading only when that channel's
                     * signal flag says there is signal, so leaving the flags
                     * true kept the last HR/SpO2/drip numbers on screen,
                     * looking current, after the device was unplugged - only
                     * the small status chip said Offline. A frozen number that
                     * looks live is worse than no number: it is the one a nurse
                     * glances at and believes.
                     *
                     * The readings themselves are deliberately left in place.
                     * The trend chart is drawn from them and its history should
                     * not be erased by a dropout; the flags are what decide
                     * whether they are presented as current.
                     *
                     * The alarm fields go too. An alert level is a verdict the
                     * device made about a patient it can no longer see, and
                     * once it is out of contact it cannot stand behind it. */
                    var updated = bedStateStore.Upsert(bed.BedId, state =>
                    {
                        state.Status = BedStatus.Offline;

                        state.HeartRateSignal = false;
                        state.Spo2Signal = false;
                        state.FlowSignal = false;
                        state.DripRateSignal = false;

                        state.AlertsArmed = false;
                        state.AlertLevel = null;
                        state.FinalAlertLevel = null;
                        state.AlertMessage = null;
                        state.LineBranch = false;
                        state.PatientBranch = false;
                        state.LineBlocked = false;
                        state.AeAlarm = false;
                        state.DripAnomaly = false;
                        state.VitalsAnomaly = false;
                        state.TsAnomaly = false;
                        state.TsEarlyWarning = false;

                        /* The branch verdicts and the AI's inputs are things
                         * the device reported about a moment it can no longer
                         * see. Same reasoning as the signal flags. */
                        state.VitalsLevel = null;
                        state.ServerDropLevel = null;
                        state.AiInputHeartRate = null;
                        state.AiInputSpo2 = null;
                        state.Spo2Low = false;
                        state.HeartRateAbnormal = false;
                        state.DropIntervalMs = null;
                    });

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
