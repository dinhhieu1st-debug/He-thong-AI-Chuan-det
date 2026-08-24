using HisServer.Data;
using HisServer.Domain;
using HisServer.Models;
using HisServer.Services;

namespace HisServer.Api;

/// <summary>Accounts and duty rosters. Administrators only.</summary>
public static class UserEndpoints
{
    public sealed record CreateUserRequest(string Username, string Password, string? FullName, string Role);
    public sealed record UpdateUserRequest(string? FullName, string? Role, bool? IsActive);
    public sealed record ResetPasswordRequest(string NewPassword);
    public sealed record AssignRequest(string ScopeType, string ScopeValue);

    private const int MinPasswordLength = 8;

    public static void MapUserEndpoints(this WebApplication app)
    {
        var group = app.MapGroup("/api/users")
            .RequireAuthorization(Capabilities.ManageUsers);

        group.MapGet("/", async (UserRepository users) =>
        {
            var all = await users.GetAllAsync();
            // A password hash is not something an API returns, ever - not even
            // to an administrator, and not even over localhost.
            return Results.Ok(all.Select(Describe));
        });

        group.MapGet("/{userId:int}", async (int userId, UserRepository users) =>
        {
            var user = await users.FindByIdAsync(userId);
            if (user is null) return Results.NotFound();
            var assignments = await users.GetAssignmentsAsync(userId);
            return Results.Ok(new { user = Describe(user), assignments = assignments.Select(DescribeAssignment) });
        });

        group.MapPost("/", async (CreateUserRequest request, UserRepository users) =>
        {
            var username = (request.Username ?? string.Empty).Trim();
            if (username.Length < 3)
            {
                return Results.BadRequest(new { error = "Username must be at least 3 characters" });
            }
            if ((request.Password ?? string.Empty).Length < MinPasswordLength)
            {
                return Results.BadRequest(new
                {
                    error = $"Password must be at least {MinPasswordLength} characters"
                });
            }
            if (!TryParseRole(request.Role, out var role))
            {
                return Results.BadRequest(new { error = "Unknown role" });
            }
            if (await users.FindByUsernameAsync(username) is not null)
            {
                return Results.Conflict(new { error = $"Account '{username}' already exists" });
            }

            var id = await users.CreateAsync(username, PasswordHasher.Hash(request.Password!),
                                             request.FullName?.Trim() ?? string.Empty, role);
            return Results.Created($"/api/users/{id}", new { userId = id });
        });

        group.MapPut("/{userId:int}", async (
            int userId, UpdateUserRequest request, UserRepository users, HttpContext http) =>
        {
            var user = await users.FindByIdAsync(userId);
            if (user is null) return Results.NotFound();

            var role = user.Role;
            if (request.Role is not null && !TryParseRole(request.Role, out role))
            {
                return Results.BadRequest(new { error = "Unknown role" });
            }

            var isActive = request.IsActive ?? user.IsActive;

            /* Guard against an administrator locking everyone out by demoting
             * or disabling themselves - the only way back from that is editing
             * the database by hand. */
            var self = await AuthEndpoints.CurrentUserAsync(http, users);
            if (self is not null && self.UserId == userId
                && (role != UserRole.Admin || !isActive))
            {
                return Results.BadRequest(new
                {
                    error = "You cannot demote or disable the account you are signed in with"
                });
            }

            await users.UpdateAsync(userId, request.FullName?.Trim() ?? user.FullName, role, isActive);
            return Results.Ok(new { ok = true });
        });

        group.MapPost("/{userId:int}/reset-password", async (
            int userId, ResetPasswordRequest request, UserRepository users) =>
        {
            if ((request.NewPassword ?? string.Empty).Length < MinPasswordLength)
            {
                return Results.BadRequest(new
                {
                    error = $"Password must be at least {MinPasswordLength} characters"
                });
            }
            if (await users.FindByIdAsync(userId) is null) return Results.NotFound();

            // mustChange: the owner has not picked this password, an admin did.
            await users.SetPasswordAsync(userId, PasswordHasher.Hash(request.NewPassword!),
                                         mustChange: true);
            return Results.Ok(new { ok = true });
        });

        group.MapDelete("/{userId:int}", async (int userId, UserRepository users, HttpContext http) =>
        {
            var self = await AuthEndpoints.CurrentUserAsync(http, users);
            if (self is not null && self.UserId == userId)
            {
                return Results.BadRequest(new { error = "You cannot delete the account you are signed in with" });
            }
            await users.DeleteAsync(userId);
            return Results.NoContent();
        });

        // ---- Duty roster ---------------------------------------------------

        group.MapGet("/{userId:int}/assignments", async (int userId, UserRepository users) =>
            Results.Ok((await users.GetAssignmentsAsync(userId)).Select(DescribeAssignment)));

        group.MapPost("/{userId:int}/assignments", async (
            int userId, AssignRequest request, UserRepository users) =>
        {
            if (await users.FindByIdAsync(userId) is null) return Results.NotFound();
            if (!Enum.TryParse<AssignmentScope>(request.ScopeType, ignoreCase: true, out var scope))
            {
                return Results.BadRequest(new { error = "scopeType must be ROOM or BED" });
            }
            var value = (request.ScopeValue ?? string.Empty).Trim();
            if (value.Length == 0)
            {
                return Results.BadRequest(new { error = "Missing room name or bed id" });
            }

            await users.AddAssignmentAsync(userId, scope, value);
            return Results.Ok(new { ok = true });
        });

        group.MapDelete("/assignments/{assignmentId:int}", async (
            int assignmentId, UserRepository users) =>
        {
            await users.RemoveAssignmentAsync(assignmentId);
            return Results.NoContent();
        });
    }

    /* Scope type goes out as "ROOM"/"BED", not as the enum's ordinal. The
     * browser compares it against those names, and a bare 0 silently reads as
     * "not ROOM" - every room assignment would render as a bed. */
    private static object DescribeAssignment(UserAssignment a) => new
    {
        assignmentId = a.AssignmentId,
        userId = a.UserId,
        scopeType = a.ScopeType.ToString().ToUpperInvariant(),
        scopeValue = a.ScopeValue,
        assignedAt = a.AssignedAt
    };

    private static bool TryParseRole(string? value, out UserRole role) =>
        Enum.TryParse(value, ignoreCase: true, out role);

    private static object Describe(UserRecord user) => new
    {
        userId = user.UserId,
        username = user.Username,
        fullName = user.FullName,
        role = user.Role.ToString().ToUpperInvariant(),
        isActive = user.IsActive,
        mustChangePassword = user.MustChangePassword,
        createdAt = user.CreatedAt,
        lastLoginAt = user.LastLoginAt
    };
}
