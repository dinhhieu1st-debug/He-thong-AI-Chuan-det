using HisServer.Data;
using HisServer.Domain;
using HisServer.Models;

namespace HisServer.Api;

/// <summary>
/// "My shift": which rooms and beds this user is responsible for, and who is
/// currently in those beds.
///
/// The roster is informational, NOT a filter on what the user may see (see the
/// comment on user_assignments in the migration). A nurse still opens any bed
/// in the ward; this page answers "which ones are mine right now", which is
/// otherwise something they have to keep in their head.
/// </summary>
public static class ProfileEndpoints
{
    public static void MapProfileEndpoints(this WebApplication app)
    {
        app.MapGet("/api/me/assignment", async (
            HttpContext http,
            UserRepository users,
            BedStateStore beds) =>
        {
            var user = await AuthEndpoints.CurrentUserAsync(http, users);
            if (user is null) return Results.Unauthorized();

            var assignments = await users.GetAssignmentsAsync(user.UserId);
            var rooms = assignments.Where(a => a.ScopeType == AssignmentScope.Room)
                                   .Select(a => a.ScopeValue).ToHashSet(StringComparer.OrdinalIgnoreCase);
            var bedIds = assignments.Where(a => a.ScopeType == AssignmentScope.Bed)
                                    .Select(a => a.ScopeValue).ToHashSet(StringComparer.OrdinalIgnoreCase);

            /* A bed counts as mine if the whole room is mine, or the bed itself
             * was assigned individually - a patient moved mid-shift should not
             * need the roster rewritten to show up here. */
            var myBeds = beds.GetAll()
                .Where(b => rooms.Contains(b.Room) || bedIds.Contains(b.BedId))
                .OrderBy(b => b.BedId, StringComparer.OrdinalIgnoreCase)
                .Select(b => new
                {
                    bedId = b.BedId,
                    room = b.Room,
                    status = b.Status.ToString(),
                    patientName = b.PatientName,
                    patientCode = b.PatientCode,
                    admittedAt = b.AdmittedAt,
                    alertMessage = b.AlertMessage,
                    lastUpdated = b.LastDataAt,
                    // Enough vitals for the page to be worth opening on its own.
                    spo2 = b.Spo2,
                    heartRate = b.HeartRate
                })
                .ToList();

            return Results.Ok(new
            {
                user = new
                {
                    username = user.Username,
                    fullName = user.FullName,
                    role = user.Role.ToString().ToUpperInvariant(),
                    lastLoginAt = user.LastLoginAt
                },
                rooms = rooms.OrderBy(r => r, StringComparer.OrdinalIgnoreCase).ToList(),
                individualBeds = bedIds.OrderBy(b => b, StringComparer.OrdinalIgnoreCase).ToList(),
                beds = myBeds,
                summary = new
                {
                    total = myBeds.Count,
                    critical = myBeds.Count(b => b.status == "Critical"),
                    warning = myBeds.Count(b => b.status == "Warning"),
                    occupied = myBeds.Count(b => !string.IsNullOrWhiteSpace(b.patientName as string))
                }
            });
        }).RequireAuthorization(Capabilities.ViewWard);
    }
}
