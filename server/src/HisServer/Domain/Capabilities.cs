using HisServer.Models;

namespace HisServer.Domain;

/// <summary>
/// What each role is allowed to DO, in one place.
///
/// Endpoints are guarded by CAPABILITY ("can control a bed"), never by role
/// name ("is a nurse"). Adding the doctor role that is already planned then
/// means adding one line to each capability below - not hunting for
/// <c>RequireRole("NURSE")</c> scattered across six endpoint files, where the
/// one that gets missed is a permission bug nobody notices until it matters.
///
/// The same names are sent to the browser by <c>/api/auth/me</c>, so the UI
/// hides exactly what the API refuses. Hiding a button is only cosmetic - the
/// API check is the real boundary - but the two must agree, or the UI offers
/// actions that then fail.
/// </summary>
public static class Capabilities
{
    /// <summary>See the ward: dashboard, bed detail, trends, alerts.</summary>
    public const string ViewWard = "ward.view";

    /// <summary>Acknowledge an alert.</summary>
    public const string AckAlerts = "alerts.ack";

    /// <summary>Act on the device at the bedside: infusion targets, tare the
    /// scale, restart the heart-rate baseline.</summary>
    public const string ControlBed = "bed.control";

    /// <summary>Record who is in the bed.</summary>
    public const string EditPatient = "patient.edit";

    /// <summary>Add/edit/remove physical devices.</summary>
    public const string ManageDevices = "devices.manage";

    /// <summary>System Log, including the CSV export.</summary>
    public const string ViewLogs = "logs.view";

    /// <summary>Create beds, change a bed's room.</summary>
    public const string ManageBeds = "beds.manage";

    /// <summary>Accounts and duty rosters.</summary>
    public const string ManageUsers = "users.manage";

    private static readonly IReadOnlyDictionary<UserRole, string[]> ByRole =
        new Dictionary<UserRole, string[]>
        {
            /* A nurse can act on the bedside device. Tare in particular is not
             * optional for them: the nurse is the one who hangs the bag, and
             * the scale reads nothing useful until it is zeroed afterwards. Had
             * that needed an administrator, the ward would simply have shared
             * the admin account - and shared accounts end the usefulness of
             * having roles at all. */
            [UserRole.Nurse] = new[]
            {
                ViewWard, AckAlerts, ControlBed, EditPatient
            },

            /* Technicians keep the hardware alive; they have no business
             * silencing a clinical alarm or naming a patient. System Log is
             * theirs because diagnosing "why did this bed go offline" is
             * exactly their job. */
            [UserRole.Technician] = new[]
            {
                ViewWard, ManageDevices, ViewLogs
            },

            [UserRole.Admin] = new[]
            {
                ViewWard, AckAlerts, ControlBed, EditPatient,
                ManageDevices, ViewLogs, ManageBeds, ManageUsers
            }
        };

    public static string[] For(UserRole role) =>
        ByRole.TryGetValue(role, out var caps) ? caps : Array.Empty<string>();

    public static bool Has(UserRole role, string capability) =>
        For(role).Contains(capability);

    /// <summary>Every capability name, used to register one authorization
    /// policy per capability at startup.</summary>
    public static readonly string[] All =
    {
        ViewWard, AckAlerts, ControlBed, EditPatient,
        ManageDevices, ViewLogs, ManageBeds, ManageUsers
    };
}
