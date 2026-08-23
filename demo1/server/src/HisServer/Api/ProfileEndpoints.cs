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

            /* The roster's bed list carries patient names, so it is built only
             * for roles that may see the ward at all. A technician opening
             * their own profile page gets their account details and an empty
             * list - not a directory of who is in which bed. */
            var canSeeWard = Capabilities.Has(user.Role, Capabilities.ViewWard);

            /* EVERY bed the nurse can see, not just the rostered ones.
             *
             * The roster used to filter this list, which made the page
             * disagree with the rest of the console: the Beds tab showed nine
             * beds and this page showed two, with nothing on screen explaining
             * why. Since the roster was never a permission - a nurse may open
             * any bed in the ward - a shorter list here was not protecting
             * anything, it was just a second, quieter answer to "how many beds
             * are there".
             *
             * The roster still means something: beds in it are marked and
             * sorted to the top, so "mine right now" is still answerable at a
             * glance. A bed counts as mine if the whole room is mine, or the
             * bed itself was assigned individually - a patient moved mid-shift
             * should not need the roster rewritten to show up here. */
            var myBeds = !canSeeWard ? new() : beds.GetAll()
                .Select(b => new
                {
                    mine = rooms.Contains(b.Room) || bedIds.Contains(b.BedId),
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
                .OrderByDescending(b => b.mine)
                .ThenBy(b => b.bedId, StringComparer.OrdinalIgnoreCase)
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
                    /* Kept separate so the page can say "9 beds, 2 of them
                     * yours" rather than making the reader choose which number
                     * the tiles mean. */
                    mine = myBeds.Count(b => b.mine),
                    critical = myBeds.Count(b => b.status == "Critical"),
                    warning = myBeds.Count(b => b.status == "Warning"),
                    occupied = myBeds.Count(b => !string.IsNullOrWhiteSpace(b.patientName as string))
                }
            });
        }).RequireAuthorization();   // any signed-in user has a profile
    }
}
