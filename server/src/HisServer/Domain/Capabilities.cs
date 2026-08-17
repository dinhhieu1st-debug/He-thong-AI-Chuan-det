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

    /// <summary>Raise a "this equipment is broken" report from the bedside.</summary>
    public const string ReportFaults = "faults.report";

    /// <summary>Pick up and close those reports.</summary>
    public const string HandleFaults = "faults.handle";

    /// <summary>
    /// Read the bed DIRECTORY - identifiers and rooms, no vitals and no patient.
    ///
    /// Split out from ViewWard because a technician assigning a device to a bed
    /// needs the list of bed numbers, and an administrator building the ward
    /// needs it too, but neither has any business seeing what the patient in
    /// that bed reads.
    /// </summary>
    public const string ViewBedDirectory = "beds.directory";

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
                ViewWard, AckAlerts, ControlBed, EditPatient, ReportFaults, ViewBedDirectory
            },

            /* Technicians keep the hardware alive and see NOTHING clinical.
             *
             * They lost ViewWard and ViewLogs on purpose. A technician does not
             * need a patient's SpO2 to decide whether to replace a cable, and
             * the System Log embeds SpO2/HR in every row. What replaced them is
             * better for the job anyway: device health derived from the live
             * stream, a device-only event log, and the fault queue - all of
             * which say which box is broken without saying anything about who
             * is lying in the bed.
             *
             * ViewBedDirectory is bed numbers and rooms only: needed to assign
             * a device to a bed, useless for learning anything about a patient. */
            [UserRole.Technician] = new[]
            {
                ManageDevices, HandleFaults, ViewBedDirectory
            },

            /* Administration is accounts, rosters and the ward's structure -
             * not patient monitoring. An administrator who can also read every
             * bed is the "knows everything" account: the one an attacker aims
             * at, and the one an audit asks awkward questions about. They keep
             * System Log because reconciling an incident is their job, and that
             * is the single place they meet clinical data. */
            [UserRole.Admin] = new[]
            {
                ViewBedDirectory, ManageBeds, ViewLogs, ManageUsers
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
        ManageDevices, ViewLogs, ManageBeds, ManageUsers,
        ReportFaults, HandleFaults, ViewBedDirectory
    };
}
