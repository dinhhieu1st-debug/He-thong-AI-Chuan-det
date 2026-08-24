namespace HisServer.Models;

/// <summary>
/// Where one device is in a firmware update.
///
/// Kept in memory only, deliberately. An update takes minutes and never
/// survives a server restart in a useful form: if the server goes down
/// mid-update the transfer is gone too, and replaying a stale "updating, 40%"
/// from a database after a restart would show a technician progress for
/// something that is no longer happening.
/// </summary>
public enum OtaState
{
    /// <summary>Nothing known. The device may or may not support OTA at all.</summary>
    Unknown = 0,

    /// <summary>Checked, and the firmware on the device is the newest offered.</summary>
    UpToDate = 1,

    /// <summary>Checked, and a newer image exists.</summary>
    Available = 2,

    /// <summary>The technician pressed Update; the request has gone down but
    /// no bytes have been reported moving yet.</summary>
    Starting = 3,

    /// <summary>Image transfer in progress.</summary>
    Updating = 4,

    /// <summary>Transfer finished; the device reboots into the new image.</summary>
    Done = 5,

    /// <summary>The update failed. <see cref="OtaStatus.Message"/> says why.</summary>
    Failed = 6,
}

/// <summary>
/// One device's OTA state, as last reported by the gateway.
/// </summary>
public sealed record OtaStatus(
    string DeviceId,
    OtaState State,

    /// <summary>0-100, or null when not transferring. Null rather than 0: "not
    /// started" and "started but nothing moved yet" look identical at 0, and a
    /// progress bar that sits at 0% is indistinguishable from one that is
    /// stuck.</summary>
    int? Progress,

    /// <summary>Seconds left as estimated by zigbee2mqtt, or null.</summary>
    int? RemainingSeconds,

    /// <summary>Failure reason, or any note worth showing. Empty when there is
    /// nothing to say.</summary>
    string? Message,

    DateTime UpdatedAt)
{
    /// <summary>
    /// True while an update is actually in flight.
    ///
    /// The UI uses this to hide the Update button. Offering it again during a
    /// transfer invites a technician to press it twice, and a second image
    /// transfer starting on top of the first is the one way this feature can
    /// leave a bedside device with half a firmware.
    /// </summary>
    public bool InFlight => State is OtaState.Starting or OtaState.Updating;

    public static OtaStatus Unknown(string deviceId) =>
        new(deviceId, OtaState.Unknown, null, null, null, DateTime.UtcNow);

    /// <summary>
    /// Maps the wire strings the gateway forwards - a mix of zigbee2mqtt's own
    /// vocabulary and the two the gateway adds itself - onto the enum.
    /// </summary>
    public static OtaState ParseState(string? raw) => raw?.ToLowerInvariant() switch
    {
        "uptodate" or "idle" => OtaState.UpToDate,
        "available" => OtaState.Available,
        "starting" => OtaState.Starting,
        "updating" or "downloading" => OtaState.Updating,
        "done" or "updated" or "success" => OtaState.Done,
        "failed" or "error" => OtaState.Failed,
        _ => OtaState.Unknown,
    };
}
