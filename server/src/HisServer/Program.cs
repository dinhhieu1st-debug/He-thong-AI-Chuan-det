using HisServer.Api;
using HisServer.Data;
using HisServer.Domain;
using HisServer.Hubs;
using HisServer.Ingestion;
using HisServer.Services;

var builder = WebApplication.CreateBuilder(args);

// Configuration-bound options (see appsettings.json). No hardcoded fallback
// credentials/ports — everything below comes from config or fails fast.
builder.Services.Configure<TcpOptions>(builder.Configuration.GetSection("Tcp"));
builder.Services.Configure<OfflineOptions>(builder.Configuration.GetSection("Offline"));
builder.Services.Configure<VitalsSaveOptions>(builder.Configuration.GetSection("VitalsSave"));
builder.Services.Configure<FirebaseOptions>(builder.Configuration.GetSection("Firebase"));

// Data access
builder.Services.AddSingleton<DbConnectionFactory>();
builder.Services.AddSingleton<BedRepository>();
builder.Services.AddSingleton<DeviceRepository>();
builder.Services.AddSingleton<VitalSampleRepository>();
builder.Services.AddSingleton<AlertRepository>();
builder.Services.AddSingleton<FcmTokenRepository>();
builder.Services.AddSingleton<LogRepository>();

// Domain / services
builder.Services.AddSingleton<BedStateStore>();
builder.Services.AddSingleton<BedConnectionRegistry>();
builder.Services.AddSingleton<VitalsPersistenceCoordinator>();
builder.Services.AddHttpClient<FcmPushService>();

// Real-time + background ingestion
builder.Services.AddSignalR();
builder.Services.AddHostedService<BedTcpIngestionService>();
builder.Services.AddHostedService<OfflineScanService>();

builder.Services.AddCors(o => o.AddDefaultPolicy(p => p
    .AllowAnyHeader()
    .AllowAnyMethod()
    .SetIsOriginAllowed(_ => true)
    .AllowCredentials()));

var app = builder.Build();

app.UseCors();
app.UseDefaultFiles();

/* The UI is plain <script src="js/…"> files with no bundler, so there are no
 * content-hashed filenames to bust a stale cache with. Without an explicit
 * Cache-Control, browsers fall back to HEURISTIC freshness (roughly 10% of the
 * file's age) and will happily keep serving a cached beds.js WITHOUT even
 * revalidating - so a UI change can ship, be served correctly by the server,
 * and still be invisible in the browser until a manual hard-refresh. That is
 * a genuinely confusing failure mode (it looks like the feature was never
 * deployed), so force revalidation instead: the ETag still makes the common
 * case a cheap 304 with no body transfer. */
app.UseStaticFiles(new StaticFileOptions
{
    OnPrepareResponse = ctx =>
    {
        ctx.Context.Response.Headers.CacheControl = "no-cache, must-revalidate";
    }
});

app.MapGet("/healthz", () => Results.Ok(new { status = "ok" }));
app.MapHub<MonitoringHub>("/hubs/monitoring");

app.MapBedEndpoints();
app.MapAlertEndpoints();
app.MapDeviceEndpoints();
app.MapMobileEndpoints();
app.MapLogEndpoints();
app.MapSettingsEndpoints();

// Restore in-memory bed state from MySQL on startup so a restart doesn't
// briefly show an empty dashboard.
using (var scope = app.Services.CreateScope())
{
    var bedRepository = scope.ServiceProvider.GetRequiredService<BedRepository>();
    var bedStateStore = scope.ServiceProvider.GetRequiredService<BedStateStore>();
    var logger = scope.ServiceProvider.GetRequiredService<ILogger<Program>>();

    try
    {
        var beds = await bedRepository.LoadAllAsync();
        foreach (var bed in beds)
        {
            bedStateStore.Upsert(bed.BedId, state =>
            {
                state.Room = bed.Room;
                state.Status = bed.Status;
                state.Spo2 = bed.Spo2;
                state.HeartRate = bed.HeartRate;
                state.Temperature = bed.Temperature;
                state.DripRate = bed.DripRate;
                state.FlowRate = bed.FlowRate;
                state.HeartRateSignal = bed.HeartRateSignal;
                state.Spo2Signal = bed.Spo2Signal;
                state.FlowSignal = bed.FlowSignal;
                state.DripRateSignal = bed.DripRateSignal;
                state.LineBlocked = bed.LineBlocked;
                state.AeAlarm = bed.AeAlarm;
                state.WeightG = bed.WeightG;
                state.DropsPerMin = bed.DropsPerMin;
                state.TargetFlowMlH = bed.TargetFlowMlH;
                state.TargetDropsPerMin = bed.TargetDropsPerMin;
                state.TareInProgress = bed.TareInProgress;
                state.TareJustCompleted = bed.TareJustCompleted;
                state.HrBaselineJustCompleted = bed.HrBaselineJustCompleted;
                state.HrBaselineSecondsRemaining = bed.HrBaselineSecondsRemaining;
                state.HrBaselineBpm = bed.HrBaselineBpm;
                state.HrBaselineCapturedAt = bed.HrBaselineCapturedAt;
                state.LastTareCompletedAt = bed.LastTareCompletedAt;
                state.LastSeenTareEventCount = bed.LastSeenTareEventCount;
                state.LastSeenHrBaselineEventCount = bed.LastSeenHrBaselineEventCount;
                state.AlertMessage = bed.AlertMessage;
                state.DeviceId = bed.DeviceId;
                state.LastDataAt = bed.LastDataAt;
            });
        }
        logger.LogInformation("Restored {Count} bed(s) from the database.", beds.Count);
    }
    catch (Exception ex)
    {
        logger.LogWarning(ex, "Could not load existing bed state from the database at startup.");
    }
}

app.Run();
