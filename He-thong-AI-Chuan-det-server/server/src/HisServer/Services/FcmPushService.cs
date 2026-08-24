using System.Net.Http.Headers;
using System.Security.Cryptography;
using System.Text;
using System.Text.Json;
using HisServer.Data;
using HisServer.Models;
using Microsoft.Extensions.Options;

namespace HisServer.Services;

public sealed class FirebaseOptions
{
    public string ProjectId { get; set; } = string.Empty;

    /// <summary>Path to the Firebase service-account JSON file (also settable via FPT_FIREBASE_SERVICE_ACCOUNT).</summary>
    public string ServiceAccountPath { get; set; } = string.Empty;
}

/// <summary>
/// Sends FCM push notifications for newly-created alerts. Ported from the old
/// WinForms app's FcmPushService — the JWT-signing/OAuth flow is unchanged
/// (pure HttpClient + RSA, already cross-platform); device tokens now live in
/// MySQL (fcm_tokens table) instead of a local flat file.
/// </summary>
public sealed class FcmPushService
{
    private const string Scope = "https://www.googleapis.com/auth/firebase.messaging";

    private readonly FcmTokenRepository tokenRepository;
    private readonly IOptionsMonitor<FirebaseOptions> options;
    private readonly ILogger<FcmPushService> logger;
    private readonly HttpClient httpClient;

    public FcmPushService(
        FcmTokenRepository tokenRepository,
        IOptionsMonitor<FirebaseOptions> options,
        ILogger<FcmPushService> logger,
        HttpClient httpClient)
    {
        this.tokenRepository = tokenRepository;
        this.options = options;
        this.logger = logger;
        this.httpClient = httpClient;
    }

    public Task RegisterTokenAsync(string token, CancellationToken cancellationToken = default) =>
        string.IsNullOrWhiteSpace(token) ? Task.CompletedTask : tokenRepository.RegisterAsync(token.Trim(), cancellationToken);

    public async Task SendAlertAsync(AlertRecord alert, CancellationToken cancellationToken = default)
    {
        var serviceAccountPath = ResolveServiceAccountPath();
        var projectId = options.CurrentValue.ProjectId;

        if (string.IsNullOrWhiteSpace(projectId) || !File.Exists(serviceAccountPath))
        {
            logger.LogInformation("Firebase service account not configured; skipping push for alert {AlertId}.", alert.AlertId);
            return;
        }

        var tokens = await tokenRepository.GetAllTokensAsync(cancellationToken);
        if (tokens.Count == 0)
        {
            logger.LogInformation("No registered mobile devices; skipping push for alert {AlertId}.", alert.AlertId);
            return;
        }

        string accessToken;
        try
        {
            accessToken = await CreateAccessTokenAsync(serviceAccountPath, cancellationToken);
        }
        catch (Exception ex)
        {
            logger.LogWarning(ex, "Failed to obtain FCM OAuth access token.");
            return;
        }

        var endpoint = $"https://fcm.googleapis.com/v1/projects/{projectId}/messages:send";
        var failedTokens = new List<string>();

        foreach (var token in tokens)
        {
            var payload = new
            {
                message = new
                {
                    token,
                    notification = new
                    {
                        title = $"{alert.Level} - {alert.BedId}",
                        body = alert.Message
                    },
                    data = new Dictionary<string, string>
                    {
                        ["type"] = "bed_alert",
                        ["alertId"] = alert.AlertId.ToString(),
                        ["bedId"] = alert.BedId,
                        ["room"] = alert.Room ?? string.Empty,
                        ["level"] = alert.Level.ToString(),
                        ["message"] = alert.Message,
                        ["createdAt"] = alert.CreatedAt.ToString("yyyy-MM-ddTHH:mm:ss")
                    },
                    android = new
                    {
                        priority = "HIGH",
                        notification = new { channel_id = "bed_alerts", sound = "default" }
                    }
                }
            };

            using var request = new HttpRequestMessage(HttpMethod.Post, endpoint);
            request.Headers.Authorization = new AuthenticationHeaderValue("Bearer", accessToken);
            request.Content = new StringContent(JsonSerializer.Serialize(payload), Encoding.UTF8, "application/json");

            try
            {
                using var response = await httpClient.SendAsync(request, cancellationToken);
                if (!response.IsSuccessStatusCode)
                {
                    var error = await response.Content.ReadAsStringAsync(cancellationToken);
                    logger.LogWarning("FCM push failed with HTTP {StatusCode}: {Error}", (int)response.StatusCode, Truncate(error, 160));
                    if ((int)response.StatusCode is 404 or 400)
                    {
                        failedTokens.Add(token);
                    }
                }
            }
            catch (Exception ex)
            {
                logger.LogWarning(ex, "Failed to send FCM push notification.");
            }
        }

        foreach (var failedToken in failedTokens)
        {
            await tokenRepository.RemoveAsync(failedToken, cancellationToken);
        }
    }

    private async Task<string> CreateAccessTokenAsync(string serviceAccountPath, CancellationToken cancellationToken)
    {
        using var document = JsonDocument.Parse(await File.ReadAllTextAsync(serviceAccountPath, cancellationToken));
        var root = document.RootElement;
        var clientEmail = root.GetProperty("client_email").GetString() ?? string.Empty;
        var privateKey = root.GetProperty("private_key").GetString() ?? string.Empty;
        var now = DateTimeOffset.UtcNow.ToUnixTimeSeconds();

        var header = new { alg = "RS256", typ = "JWT" };
        var payload = new
        {
            iss = clientEmail,
            scope = Scope,
            aud = "https://oauth2.googleapis.com/token",
            iat = now,
            exp = now + 3600
        };

        var unsignedJwt = Base64Url(JsonSerializer.SerializeToUtf8Bytes(header))
            + "." + Base64Url(JsonSerializer.SerializeToUtf8Bytes(payload));

        using var rsa = RSA.Create();
        rsa.ImportFromPem(privateKey);
        var signature = rsa.SignData(Encoding.UTF8.GetBytes(unsignedJwt), HashAlgorithmName.SHA256, RSASignaturePadding.Pkcs1);
        var assertion = unsignedJwt + "." + Base64Url(signature);

        using var request = new HttpRequestMessage(HttpMethod.Post, "https://oauth2.googleapis.com/token")
        {
            Content = new FormUrlEncodedContent(new Dictionary<string, string>
            {
                ["grant_type"] = "urn:ietf:params:oauth:grant-type:jwt-bearer",
                ["assertion"] = assertion
            })
        };

        using var response = await httpClient.SendAsync(request, cancellationToken);
        var json = await response.Content.ReadAsStringAsync(cancellationToken);
        if (!response.IsSuccessStatusCode)
        {
            throw new InvalidOperationException($"OAuth token request failed with HTTP {(int)response.StatusCode}: {Truncate(json, 160)}");
        }

        using var tokenDocument = JsonDocument.Parse(json);
        return tokenDocument.RootElement.GetProperty("access_token").GetString() ?? string.Empty;
    }

    private static string Base64Url(byte[] data) =>
        Convert.ToBase64String(data).TrimEnd('=').Replace('+', '-').Replace('/', '_');

    private string ResolveServiceAccountPath()
    {
        var configured = Environment.GetEnvironmentVariable("FPT_FIREBASE_SERVICE_ACCOUNT");
        if (!string.IsNullOrWhiteSpace(configured))
        {
            return configured;
        }

        return string.IsNullOrWhiteSpace(options.CurrentValue.ServiceAccountPath)
            ? Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.UserProfile), "firebase-service-account.json")
            : options.CurrentValue.ServiceAccountPath;
    }

    private static string Truncate(string value, int maxLength) =>
        value.Length <= maxLength ? value : value[..maxLength] + "...";
}
