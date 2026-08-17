using System.Security.Claims;
using HisServer.Data;
using HisServer.Domain;
using HisServer.Models;
using HisServer.Services;
using Microsoft.AspNetCore.Authentication;
using Microsoft.AspNetCore.Authentication.Cookies;

namespace HisServer.Api;

public static class AuthEndpoints
{
    public sealed record LoginRequest(string Username, string Password);
    public sealed record ChangePasswordRequest(string CurrentPassword, string NewPassword);

    /// <summary>Shortest password accepted when a user picks their own.</summary>
    private const int MinPasswordLength = 8;

    public static void MapAuthEndpoints(this WebApplication app)
    {
        var group = app.MapGroup("/api/auth");

        group.MapPost("/login", async (
            LoginRequest request,
            UserRepository users,
            HttpContext http,
            ILoggerFactory loggerFactory) =>
        {
            var logger = loggerFactory.CreateLogger("Auth");
            var user = await users.FindByUsernameAsync(request.Username ?? string.Empty);

            /* One message for every failure - unknown username, wrong password,
             * disabled account. Saying which one is wrong tells an attacker
             * which usernames exist. The log below records the attempt (never
             * the password) so a real lockout policy has something to work
             * from later. */
            if (user is null
                || !user.IsActive
                || !PasswordHasher.Verify(request.Password ?? string.Empty, user.PasswordHash))
            {
                logger.LogWarning("Failed login for '{Username}' from {Ip}",
                    request.Username, http.Connection.RemoteIpAddress);
                return Results.Json(new { error = "Incorrect username or password" },
                                    statusCode: StatusCodes.Status401Unauthorized);
            }

            await SignInAsync(http, user);
            await users.TouchLoginAsync(user.UserId);

            return Results.Ok(Describe(user));
        }).AllowAnonymous();

        group.MapPost("/logout", async (HttpContext http) =>
        {
            await http.SignOutAsync(CookieAuthenticationDefaults.AuthenticationScheme);
            return Results.Ok(new { ok = true });
        }).AllowAnonymous();

        /* The browser asks this on every page load: who am I, and what may I
         * do. 401 here is the signal to show the login page. */
        group.MapGet("/me", async (HttpContext http, UserRepository users) =>
        {
            var user = await CurrentUserAsync(http, users);
            return user is null
                ? Results.Unauthorized()
                : Results.Ok(Describe(user));
        }).AllowAnonymous();

        group.MapPost("/change-password", async (
            ChangePasswordRequest request,
            HttpContext http,
            UserRepository users) =>
        {
            var user = await CurrentUserAsync(http, users);
            if (user is null) return Results.Unauthorized();

            if (!PasswordHasher.Verify(request.CurrentPassword ?? string.Empty, user.PasswordHash))
            {
                return Results.BadRequest(new { error = "Current password is incorrect" });
            }

            var next = request.NewPassword ?? string.Empty;
            if (next.Length < MinPasswordLength)
            {
                return Results.BadRequest(new
                {
                    error = $"New password must be at least {MinPasswordLength} characters"
                });
            }
            if (next == request.CurrentPassword)
            {
                return Results.BadRequest(new { error = "New password must differ from the old one" });
            }

            await users.SetPasswordAsync(user.UserId, PasswordHasher.Hash(next), mustChange: false);

            /* The must_change_password flag lives in the cookie, so the cookie
             * has to be reissued - otherwise the user keeps being sent back to
             * the "change your password" screen until they log out. */
            user.MustChangePassword = false;
            await SignInAsync(http, user);

            return Results.Ok(new { ok = true });
        });
    }

    private static async Task SignInAsync(HttpContext http, UserRecord user)
    {
        var claims = new List<Claim>
        {
            new(ClaimTypes.NameIdentifier, user.UserId.ToString()),
            new(ClaimTypes.Name, user.Username),
            new(ClaimTypes.Role, user.Role.ToString()),
            new("full_name", user.FullName),
            new("must_change_password", user.MustChangePassword ? "1" : "0")
        };

        await http.SignInAsync(
            CookieAuthenticationDefaults.AuthenticationScheme,
            new ClaimsPrincipal(new ClaimsIdentity(claims, CookieAuthenticationDefaults.AuthenticationScheme)),
            new AuthenticationProperties { IsPersistent = true });
    }

    /// <summary>
    /// Resolves the signed-in user from the cookie, re-reading the row from the
    /// database rather than trusting the cookie's contents.
    ///
    /// The cookie is issued at login and lives for days; a role change, or an
    /// account being disabled, must take effect on the next request, not at the
    /// next login. Re-reading is what makes "revoke access" actually revoke it.
    /// </summary>
    public static async Task<UserRecord?> CurrentUserAsync(HttpContext http, UserRepository users)
    {
        var idClaim = http.User.FindFirst(ClaimTypes.NameIdentifier)?.Value;
        if (!int.TryParse(idClaim, out var userId)) return null;

        var user = await users.FindByIdAsync(userId);
        return user is { IsActive: true } ? user : null;
    }

    private static object Describe(UserRecord user) => new
    {
        userId = user.UserId,
        username = user.Username,
        fullName = user.FullName,
        role = user.Role.ToString().ToUpperInvariant(),
        mustChangePassword = user.MustChangePassword,
        capabilities = Capabilities.For(user.Role)
    };
}
