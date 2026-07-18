using System.Net;
using System.Net.Sockets;
using System.Text;
using HisServer.Data;
using HisServer.Domain;
using HisServer.Hubs;
using HisServer.Models;
using HisServer.Services;
using Microsoft.AspNetCore.SignalR;
using Microsoft.Extensions.Options;

namespace HisServer.Ingestion;

public sealed class TcpOptions
{
    public int Port { get; set; } = 5000;
}

/// <summary>
/// Listens for bed vitals over a raw TCP socket — newline-delimited JSON, one
/// connection per device/gateway. Replaces the old WinForms app's
/// BedDataReceiver.cs; the wire protocol is unchanged (same alias-tolerant
/// field names in BedDataParser) since device firmware can't be modified.
/// </summary>
public sealed class BedTcpIngestionService : BackgroundService
{
    private readonly BedStateStore bedStateStore;
    private readonly BedRepository bedRepository;
    private readonly VitalSampleRepository vitalSampleRepository;
    private readonly AlertRepository alertRepository;
    private readonly VitalsPersistenceCoordinator persistenceCoordinator;
    private readonly FcmPushService fcmPushService;
    private readonly IHubContext<MonitoringHub, IMonitoringClient> hub;
    private readonly IOptionsMonitor<TcpOptions> options;
    private readonly ILogger<BedTcpIngestionService> logger;

    public BedTcpIngestionService(
        BedStateStore bedStateStore,
        BedRepository bedRepository,
        VitalSampleRepository vitalSampleRepository,
        AlertRepository alertRepository,
        VitalsPersistenceCoordinator persistenceCoordinator,
        FcmPushService fcmPushService,
        IHubContext<MonitoringHub, IMonitoringClient> hub,
        IOptionsMonitor<TcpOptions> options,
        ILogger<BedTcpIngestionService> logger)
    {
        this.bedStateStore = bedStateStore;
        this.bedRepository = bedRepository;
        this.vitalSampleRepository = vitalSampleRepository;
        this.alertRepository = alertRepository;
        this.persistenceCoordinator = persistenceCoordinator;
        this.fcmPushService = fcmPushService;
        this.hub = hub;
        this.options = options;
        this.logger = logger;
    }

    protected override async Task ExecuteAsync(CancellationToken stoppingToken)
    {
        var port = options.CurrentValue.Port;
        var listener = new TcpListener(IPAddress.Any, port);
        listener.Start();
        logger.LogInformation("Bed vitals TCP ingestion listening on port {Port}.", port);

        try
        {
            while (!stoppingToken.IsCancellationRequested)
            {
                TcpClient client;
                try
                {
                    client = await listener.AcceptTcpClientAsync(stoppingToken);
                }
                catch (OperationCanceledException)
                {
                    break;
                }

                _ = HandleClientAsync(client, stoppingToken);
            }
        }
        finally
        {
            listener.Stop();
        }
    }

    private async Task HandleClientAsync(TcpClient client, CancellationToken cancellationToken)
    {
        using (client)
        await using (var stream = client.GetStream())
        using (var reader = new StreamReader(stream, Encoding.UTF8))
        {
            while (!cancellationToken.IsCancellationRequested)
            {
                string? line;
                try
                {
                    line = await reader.ReadLineAsync(cancellationToken);
                }
                catch (IOException)
                {
                    return;
                }

                if (line is null)
                {
                    return;
                }

                if (string.IsNullOrWhiteSpace(line))
                {
                    continue;
                }

                try
                {
                    var reading = BedDataParser.Parse(line);
                    await ProcessReadingAsync(reading, cancellationToken);
                }
                catch (Exception ex)
                {
                    logger.LogWarning(ex, "Invalid bed data line received: {Line}", line);
                }
            }
        }
    }

    private async Task ProcessReadingAsync(BedReading reading, CancellationToken cancellationToken)
    {
        var status = VitalsStatusEvaluator.Evaluate(reading);
        var previousStatus = bedStateStore.Get(reading.BedId)?.Status ?? BedStatus.Offline;

        string? alertMessage = null;
        string alertType = string.Empty;
        if (status is BedStatus.Warning or BedStatus.Critical)
        {
            (alertType, alertMessage) = VitalsStatusEvaluator.DescribeAlert(reading);
        }

        var bed = bedStateStore.Upsert(reading.BedId, state =>
        {
            state.Room = reading.Room;
            state.Status = status;
            state.Spo2 = reading.Spo2;
            state.HeartRate = reading.HeartRate;
            state.Temperature = reading.Temperature;
            state.DripRate = reading.DripRate;
            state.FlowRate = reading.FlowRate;
            state.HeartRateSignal = reading.HeartRateSignal;
            state.Spo2Signal = reading.Spo2Signal;
            state.FlowSignal = reading.FlowSignal;
            state.DripRateSignal = reading.DripRateSignal;
            state.LineBlocked = reading.LineBlocked;
            state.AeAlarm = reading.AeAlarm;
            state.AlertMessage = alertMessage;
            state.LastDataAt = reading.ReceivedAt;
        });

        await hub.Clients.All.BedUpdated(BedDto.From(bed));

        // The `beds` row is the durable "current state" record (used for restart
        // recovery), so it is always kept fresh — only the `vital_samples` history
        // insert is throttled, to avoid writing a time-series row on every tick.
        await bedRepository.UpsertAsync(bed, cancellationToken);

        if (persistenceCoordinator.ShouldSave(reading.BedId, reading.ReceivedAt))
        {
            await vitalSampleRepository.SaveAsync(
                reading.BedId, bed.DeviceId, reading.Spo2, reading.HeartRate, reading.Temperature, reading.DripRate,
                status, reading.ReceivedAt, cancellationToken);
        }

        if (AlertTransitionTracker.ShouldRaiseAlert(previousStatus, status))
        {
            var alertId = await alertRepository.InsertAsync(new AlertRecord
            {
                BedId = reading.BedId,
                Room = reading.Room,
                DeviceId = bed.DeviceId,
                Level = status,
                AlertType = alertType,
                Message = alertMessage ?? string.Empty,
                Spo2 = reading.Spo2,
                HeartRate = reading.HeartRate,
                Temperature = reading.Temperature,
                DripRate = reading.DripRate,
                CreatedAt = reading.ReceivedAt
            }, cancellationToken);

            var alert = await alertRepository.GetAsync(alertId, cancellationToken);
            if (alert is not null)
            {
                await hub.Clients.All.AlertCreated(AlertDto.From(alert));
                await fcmPushService.SendAlertAsync(alert, cancellationToken);
            }
        }
    }
}
