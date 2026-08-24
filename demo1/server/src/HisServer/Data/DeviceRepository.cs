using Dapper;
using HisServer.Domain;
using HisServer.Models;

namespace HisServer.Data;

public sealed class DeviceRepository
{
    private readonly DbConnectionFactory connectionFactory;

    public DeviceRepository(DbConnectionFactory connectionFactory)
    {
        this.connectionFactory = connectionFactory;
    }

    public async Task<IReadOnlyList<DeviceRecord>> GetAllAsync(CancellationToken cancellationToken = default)
    {
        await using var connection = await connectionFactory.OpenConnectionAsync(cancellationToken);
        var rows = await connection.QueryAsync(
            "SELECT device_id, device_type, assigned_bed_id, room, status, battery_percent, rssi, eui64, last_seen_at, " +
            "link_quality, channels_lost, last_data_at " +
            "FROM devices ORDER BY device_id");

        return rows.Select(MapRow).ToList();
    }

    public async Task<DeviceRecord?> GetAsync(string deviceId, CancellationToken cancellationToken = default)
    {
        var normalized = DeviceIdentity.Normalize(deviceId);
        var compact = DeviceIdentity.Compact(deviceId);
        await using var connection = await connectionFactory.OpenConnectionAsync(cancellationToken);
        var row = await connection.QuerySingleOrDefaultAsync(
            "SELECT device_id, device_type, assigned_bed_id, room, status, battery_percent, rssi, eui64, last_seen_at, " +
            "link_quality, channels_lost, last_data_at " +
            "FROM devices WHERE LOWER(TRIM(device_id)) = @normalized " +
            "OR LOWER(TRIM(device_id)) = @compact LIMIT 1",
            new { normalized = normalized.ToLowerInvariant(), compact = compact.ToLowerInvariant() });

        return row is null ? null : MapRow(row);
    }

    public async Task UpsertAsync(DeviceRecord device, CancellationToken cancellationToken = default)
    {
        await using var connection = await connectionFactory.OpenConnectionAsync(cancellationToken);
        await connection.ExecuteAsync(
            """
            INSERT INTO devices (device_id, device_type, assigned_bed_id, room, status, battery_percent, rssi, eui64,
              last_seen_at, link_quality, channels_lost, last_data_at)
            VALUES (@DeviceId, @DeviceType, @AssignedBedId, @Room, @Status, @BatteryPercent, @Rssi, @Eui64,
              @LastSeenAt, @LinkQuality, @ChannelsLost, @LastDataAt)
            ON DUPLICATE KEY UPDATE
              device_type = VALUES(device_type),
              assigned_bed_id = VALUES(assigned_bed_id),
              room = VALUES(room),
              status = VALUES(status),
              battery_percent = VALUES(battery_percent),
              rssi = VALUES(rssi),
              eui64 = VALUES(eui64),
              last_seen_at = VALUES(last_seen_at),
              link_quality = VALUES(link_quality),
              channels_lost = VALUES(channels_lost),
              last_data_at = VALUES(last_data_at)
            """,
            new
            {
                device.DeviceId,
                DeviceType = device.DeviceType.ToString().ToUpperInvariant(),
                device.AssignedBedId,
                device.Room,
                Status = ToDbStatus(device.Status),
                device.BatteryPercent,
                device.Rssi,
                device.Eui64,
                device.LastSeenAt,
                device.LinkQuality,
                device.ChannelsLost,
                device.LastDataAt
            });
    }

    public async Task<bool> DeleteAsync(string deviceId, CancellationToken cancellationToken = default)
    {
        var normalized = DeviceIdentity.Normalize(deviceId).ToLowerInvariant();
        var compact = DeviceIdentity.Compact(deviceId).ToLowerInvariant();
        await using var connection = await connectionFactory.OpenConnectionAsync(cancellationToken);
        var affected = await connection.ExecuteAsync(
            "DELETE FROM devices WHERE LOWER(TRIM(device_id)) = @normalized " +
            "OR LOWER(TRIM(device_id)) = @compact", new { normalized, compact });
        return affected > 0;
    }

    private static DeviceRecord MapRow(dynamic row) => new()
    {
        DeviceId = row.device_id,
        DeviceType = Enum.Parse<DeviceType>(MapDeviceTypeName((string)row.device_type), ignoreCase: true),
        AssignedBedId = row.assigned_bed_id,
        Room = row.room,
        // SENSOR_FAULT in the database, SensorFault in C# - strip the
        // underscore rather than teach every caller two spellings.
        Status = Enum.Parse<DeviceStatus>(((string)row.status).Replace("_", ""), ignoreCase: true),
        BatteryPercent = row.battery_percent,
        Rssi = row.rssi,
        Eui64 = row.eui64,
        LastSeenAt = row.last_seen_at,
        LinkQuality = row.link_quality,
        ChannelsLost = row.channels_lost,
        LastDataAt = row.last_data_at
    };

    private static string ToDbStatus(DeviceStatus status) => status switch
    {
        DeviceStatus.SensorFault => "SENSOR_FAULT",
        _ => status.ToString().ToUpperInvariant()
    };

    /// <summary>
    /// The REAL device assigned to a bed - one that has actually announced
    /// itself over Zigbee, identified by having an eui64.
    ///
    /// The eui64 filter is the important part. The demo seed creates a
    /// placeholder row per bed (XG26-BED-101 and friends) with no address and
    /// the bed already assigned, so a plain "first row for this bed" query
    /// happily hands back a device that does not exist. That is exactly what
    /// happened once a real device was deleted: live telemetry silently
    /// reattached to the placeholder, which then showed ONLINE with a signal
    /// strength - an equipment list stating something untrue, which is the one
    /// thing this whole feature exists to prevent.
    ///
    /// Most recently seen first, so if two real devices ever claim one bed the
    /// live one wins and the stale one is ignored rather than fought over.
    /// </summary>
    public async Task<DeviceRecord?> GetByBedAsync(string bedId, CancellationToken ct = default)
    {
        await using var connection = await connectionFactory.OpenConnectionAsync(ct);
        var row = await connection.QuerySingleOrDefaultAsync(
            "SELECT device_id, device_type, assigned_bed_id, room, status, battery_percent, rssi, eui64, last_seen_at, " +
            "link_quality, channels_lost, last_data_at " +
            "FROM devices WHERE assigned_bed_id = @bedId AND eui64 IS NOT NULL " +
            "ORDER BY last_seen_at DESC LIMIT 1", new { bedId });
        return row is null ? null : MapRow(row);
    }

    /// <summary>
    /// Writes only the health columns. Separate from UpsertAsync because this
    /// runs on the ingestion path - once a second per bed - and must not touch
    /// the fields a technician owns (assigned bed, room, device type). The same
    /// mistake with the patient columns would have blanked a patient's name on
    /// every packet.
    /// </summary>
    public async Task UpdateHealthAsync(string deviceId, DeviceStatus status, int? linkQuality,
                                        string? channelsLost, DateTime lastDataAt,
                                        CancellationToken ct = default)
    {
        await using var connection = await connectionFactory.OpenConnectionAsync(ct);
        await connection.ExecuteAsync(
            """
            UPDATE devices
               SET status = @status,
                   link_quality = @linkQuality,
                   channels_lost = @channelsLost,
                   last_data_at = @lastDataAt,
                   last_seen_at = @lastDataAt
             WHERE device_id = @deviceId
            """,
            new { deviceId, status = ToDbStatus(status), linkQuality, channelsLost, lastDataAt });
    }

    public async Task AddEventAsync(string deviceId, string? bedId, DeviceEventType type,
                                    string? detail, CancellationToken ct = default)
    {
        await using var connection = await connectionFactory.OpenConnectionAsync(ct);
        await connection.ExecuteAsync(
            "INSERT INTO device_events (device_id, bed_id, event_type, detail) " +
            "VALUES (@deviceId, @bedId, @type, @detail)",
            new
            {
                deviceId,
                bedId,
                type = type switch
                {
                    DeviceEventType.SensorFault => "SENSOR_FAULT",
                    DeviceEventType.FaultReported => "FAULT_REPORTED",
                    DeviceEventType.FaultResolved => "FAULT_RESOLVED",
                    DeviceEventType.FirmwareUpdateStarted => "FIRMWARE_UPDATE_STARTED",
                    DeviceEventType.FirmwareUpdated => "FIRMWARE_UPDATED",
                    DeviceEventType.FirmwareUpdateFailed => "FIRMWARE_UPDATE_FAILED",
                    _ => type.ToString().ToUpperInvariant()
                },
                detail
            });
    }

    public async Task<IReadOnlyList<DeviceEvent>> GetEventsAsync(string deviceId, int limit = 100,
                                                                CancellationToken ct = default)
    {
        await using var connection = await connectionFactory.OpenConnectionAsync(ct);
        var rows = await connection.QueryAsync(
            "SELECT event_id, device_id, bed_id, event_type, detail, occurred_at " +
            "FROM device_events WHERE device_id = @deviceId " +
            "ORDER BY occurred_at DESC LIMIT @limit",
            new { deviceId, limit });

        return rows.Select(r => new DeviceEvent
        {
            EventId = (long)r.event_id,
            DeviceId = (string)r.device_id,
            BedId = r.bed_id,
            EventType = Enum.Parse<DeviceEventType>(((string)r.event_type).Replace("_", ""), ignoreCase: true),
            Detail = r.detail,
            OccurredAt = (DateTime)r.occurred_at
        }).ToList();
    }

    private static string MapDeviceTypeName(string dbValue) => dbValue switch
    {
        "XG26" => nameof(DeviceType.Xg26),
        "GATEWAY" => nameof(DeviceType.Gateway),
        "BLE" => nameof(DeviceType.Ble),
        _ => dbValue
    };
}
