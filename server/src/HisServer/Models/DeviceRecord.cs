namespace HisServer.Models;

public enum DeviceType
{
    Xg26,
    Gateway,
    Ble
}

public enum DeviceStatus
{
    Online,
    Pending,
    Warning,
    Offline,

    /// <summary>
    /// Still reporting, but a sensor channel has been silent long enough to
    /// mean broken rather than briefly noisy.
    ///
    /// Kept apart from <see cref="Offline"/> because the two send a technician
    /// out with different equipment: a fault is a cable or a probe, an offline
    /// device is power, radio, or the whole unit.
    /// </summary>
    SensorFault
}

public sealed class DeviceRecord
{
    public required string DeviceId { get; init; }
    public DeviceType DeviceType { get; set; }
    public string? AssignedBedId { get; set; }
    public string? Room { get; set; }
    public DeviceStatus Status { get; set; } = DeviceStatus.Pending;
    public int? BatteryPercent { get; set; }
    public int? Rssi { get; set; }
    public string? Eui64 { get; set; }
    public DateTime? LastSeenAt { get; set; }

    /* Health derived from the live stream rather than typed in by hand. */

    /// <summary>Zigbee signal strength (0-255) as reported by zigbee2mqtt.</summary>
    public int? LinkQuality { get; set; }

    /// <summary>Channels currently without signal, e.g. "SPO2,FLOW". Null when
    /// everything enabled is reading.</summary>
    public string? ChannelsLost { get; set; }

    /// <summary>Last reading actually received from this device - distinct from
    /// LastSeenAt, which also moves when the device merely announces itself.</summary>
    public DateTime? LastDataAt { get; set; }
}

/// <summary>One line of the technician's log. Contains no vital signs by
/// design - see the migration for why.</summary>
public enum DeviceEventType
{
    Joined,
    Assigned,
    Online,
    SensorFault,
    Offline,
    FaultReported,
    FaultResolved
}

public sealed class DeviceEvent
{
    public long EventId { get; init; }
    public required string DeviceId { get; init; }
    public string? BedId { get; init; }
    public DeviceEventType EventType { get; init; }
    public string? Detail { get; init; }
    public DateTime OccurredAt { get; init; }
}

public enum FaultChannel { Hr, Spo2, Flow, Drops, Other }

public enum FaultStatus { Open, InProgress, Resolved }

public sealed class FaultReport
{
    public long ReportId { get; init; }
    public required string BedId { get; init; }
    public string? DeviceId { get; set; }
    public FaultChannel Channel { get; init; }
    public string Note { get; init; } = string.Empty;
    public required string ReportedBy { get; init; }
    public DateTime ReportedAt { get; init; }
    public FaultStatus Status { get; set; } = FaultStatus.Open;
    public string? HandledBy { get; set; }
    public DateTime? HandledAt { get; set; }
    public string? ResolutionNote { get; set; }
}
