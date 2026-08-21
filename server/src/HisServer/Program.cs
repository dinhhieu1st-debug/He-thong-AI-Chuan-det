using HisServer.Api;
using HisServer.Data;
using HisServer.Domain;
using HisServer.Hubs;
using HisServer.Ingestion;
using HisServer.Models;
using HisServer.Services;
using Microsoft.AspNetCore.Authentication.Cookies;
using Microsoft.AspNetCore.Authorization;

var builder = WebApplication.CreateBuilder(args);

// Configuration-bound options (see appsettings.json). No hardcoded fallback
// credentials/ports — everything below comes from config or fails fast.
builder.Services.Configure<TcpOptions>(builder.Configuration.GetSection("Tcp"));
builder.Services.Configure<OfflineOptions>(builder.Configuration.GetSection("Offline"));
builder.Services.Configure<VitalsSaveOptions>(builder.Configuration.GetSection("VitalsSave"));
builder.Services.Configure<DeviceHealthOptions>(builder.Configuration.GetSection("DeviceHealth"));
builder.Services.Configure<FirebaseOptions>(builder.Configuration.GetSection("Firebase"));

// Data access
builder.Services.AddSingleton<DbConnectionFactory>();
builder.Services.AddSingleton<BedRepository>();
builder.Services.AddSingleton<DeviceRepository>();
builder.Services.AddSingleton<VitalSampleRepository>();
builder.Services.AddSingleton<AlertRepository>();
builder.Services.AddSingleton<FcmTokenRepository>();
builder.Services.AddSingleton<LogRepository>();
builder.Services.AddSingleton<UserRepository>();
builder.Services.AddSingleton<FaultReportRepository>();

// Domain / services
builder.Services.AddSingleton<BedStateStore>();
builder.Services.AddSingleton<BedConnectionRegistry>();
// Firmware update progress. Singleton and in memory: a transfer does not
// survive a restart, so neither should the record of it - see OtaStatus.
builder.Services.AddSingleton<OtaStatusRegistry>();
builder.Services.AddSingleton<OtaImageStore>();
builder.Services.AddSingleton<VitalsPersistenceCoordinator>();
builder.Services.AddHttpClient<FcmPushService>();

// Real-time + background ingestion
builder.Services.AddSignalR();
builder.Services.AddHostedService<BedTcpIngestionService>();
builder.Services.AddHostedService<OfflineScanService>();
builder.Services.AddHostedService<DeviceOfflineScanService>();

/* Cookie authentication rather than tokens: the browser gets the session for
 * free on every request including the SignalR handshake, with no token to
 * store in JS (and therefore none for a cross-site script to steal). */
builder.Services
    .AddAuthentication(CookieAuthenticationDefaults.AuthenticationScheme)
    .AddCookie(options =>
    {
        options.Cookie.Name = "his_session";
        options.Cookie.HttpOnly = true;
        options.Cookie.SameSite = SameSiteMode.Lax;
        options.ExpireTimeSpan = TimeSpan.FromHours(12);   // one shift
        options.SlidingExpiration = true;
        options.LoginPath = "/login.html";

        /* An API call must fail with a status code, not a redirect to a login
         * PAGE: fetch() follows redirects, so the default behaviour hands the
         * caller a 200 with HTML in it, and the UI reports a JSON parse error
         * instead of "you are signed out". */
        options.Events.OnRedirectToLogin = ctx =>
        {
            if (ctx.Request.Path.StartsWithSegments("/api"))
            {
                ctx.Response.StatusCode = StatusCodes.Status401Unauthorized;
                return Task.CompletedTask;
            }
            ctx.Response.Redirect(ctx.RedirectUri);
            return Task.CompletedTask;
        };
        options.Events.OnRedirectToAccessDenied = ctx =>
        {
            ctx.Response.StatusCode = StatusCodes.Status403Forbidden;
            return Task.CompletedTask;
        };
    });

/* One policy per capability, built from the single table in Capabilities.cs.
 * Endpoints ask for a capability; which roles hold it is decided in exactly
 * one place. */
builder.Services.AddAuthorizationBuilder()
    .SetFallbackPolicy(new AuthorizationPolicyBuilder()
        .RequireAuthenticatedUser()
        .Build());

builder.Services.AddAuthorization(options =>
{
    foreach (var capability in Capabilities.All)
    {
        var roles = Enum.GetValues<UserRole>()
            .Where(r => Capabilities.Has(r, capability))
            .Select(r => r.ToString())
            .ToArray();

        options.AddPolicy(capability, policy => policy
            .RequireAuthenticatedUser()
            .RequireRole(roles));
    }
});

builder.Services.AddCors(o => o.AddDefaultPolicy(p => p
    .AllowAnyHeader()
    .AllowAnyMethod()
    .SetIsOriginAllowed(_ => true)
    .AllowCredentials()));

var app = builder.Build();

app.UseCors();
app.UseAuthentication();

/* Static files are served by middleware that runs before endpoint routing, so
 * the authorization above does not cover them: without this, the whole console
 * HTML/JS is readable while signed out. The page shell alone leaks no patient
 * data, but it is confusing to land on a dashboard that then fails every API
 * call, so unauthenticated navigation goes to the login page instead. */
app.Use(async (context, next) =>
{
    var path = context.Request.Path.Value ?? "/";
    var isPublic = path.StartsWith("/login", StringComparison.OrdinalIgnoreCase)
                   || path.StartsWith("/css/", StringComparison.OrdinalIgnoreCase)
                   || path.StartsWith("/js/login.js", StringComparison.OrdinalIgnoreCase)
                   || path.StartsWith("/api/", StringComparison.OrdinalIgnoreCase)
                   || path.StartsWith("/healthz", StringComparison.OrdinalIgnoreCase)
                   || path.StartsWith("/hubs/", StringComparison.OrdinalIgnoreCase);

    if (!isPublic && context.User.Identity?.IsAuthenticated != true)
    {
        context.Response.Redirect("/login.html");
        return;
    }

    await next();
});

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

app.UseRouting();
app.UseAuthorization();

app.MapGet("/healthz", () => Results.Ok(new { status = "ok" })).AllowAnonymous();
/* Any signed-in user may connect; WHAT they receive is decided by the
 * capability groups the hub puts them in (see MonitoringHub). Requiring
 * ward.view here would have cut technicians off from device and fault events
 * entirely. */
app.MapHub<MonitoringHub>("/hubs/monitoring").RequireAuthorization();

app.MapAuthEndpoints();
app.MapProfileEndpoints();
app.MapUserEndpoints();
app.MapPatientEndpoints();
app.MapFaultReportEndpoints();
app.MapBedEndpoints();
app.MapAlertEndpoints();
app.MapDeviceEndpoints();
app.MapOtaImageEndpoints();
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
                state.PatientName = bed.PatientName;
                state.PatientCode = bed.PatientCode;
                state.AdmittedAt = bed.AdmittedAt;
                state.Spo2 = bed.Spo2;
                state.HeartRate = bed.HeartRate;
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
