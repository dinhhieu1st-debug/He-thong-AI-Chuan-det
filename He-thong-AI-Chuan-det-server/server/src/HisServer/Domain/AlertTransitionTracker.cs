namespace HisServer.Domain;

/// <summary>
/// Decides when a status change warrants a new alert record + push notification.
/// Per product decision: alerts are raised only on a transition into Warning/Critical,
/// not on every tick a bed happens to be non-stable (the old app's behavior, which
/// spammed the alerts table and FCM on every single reading).
/// </summary>
public static class AlertTransitionTracker
{
    public static bool ShouldRaiseAlert(BedStatus previousStatus, BedStatus newStatus)
    {
        if (newStatus != BedStatus.Warning && newStatus != BedStatus.Critical)
        {
            return false;
        }

        return previousStatus != newStatus;
    }
}
