using HisServer.Services;

namespace HisServer.Api;

public static class MobileEndpoints
{
    public static void MapMobileEndpoints(this IEndpointRouteBuilder app)
    {
        app.MapPost("/api/mobile/register-token", async (RegisterTokenRequest request, FcmPushService fcmPushService) =>
        {
            if (string.IsNullOrWhiteSpace(request.Token))
            {
                return Results.BadRequest(new { error = "Missing FCM token" });
            }

            await fcmPushService.RegisterTokenAsync(request.Token);
            return Results.Ok(new { ok = true });
        });
    }
}

public sealed record RegisterTokenRequest(string Token);
