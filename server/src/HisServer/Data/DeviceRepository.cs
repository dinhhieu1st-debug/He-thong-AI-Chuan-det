using Dapper;
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
            "SELECT device_id, device_type, assigned_bed_id, room, status, battery_percent, rssi, eui64, last_seen_at " +
            "FROM devices ORDER BY device_id");

        return rows.Select(MapRow).ToList();
    }

    public async Task<DeviceRecord?> GetAsync(string deviceId, CancellationToken cancellationToken = default)
    {
        await using var connection = await connectionFactory.OpenConnectionAsync(cancellationToken);
        var row = await connection.QuerySingleOrDefaultAsync(
            "SELECT device_id, device_type, assigned_bed_id, room, status, battery_percent, rssi, eui64, last_seen_at " +
            "FROM devices WHERE device_id = @deviceId",
            new { deviceId });

        return row is null ? null : MapRow(row);
    }

    public async Task UpsertAsync(DeviceRecord device, CancellationToken cancellationToken = default)
    {
        await using var connection = await connectionFactory.OpenConnectionAsync(cancellationToken);
        await connection.ExecuteAsync(
            """
            INSERT INTO devices (device_id, device_type, assigned_bed_id, room, status, battery_percent, rssi, eui64, last_seen_at)
            VALUES (@DeviceId, @DeviceType, @AssignedBedId, @Room, @Status, @BatteryPercent, @Rssi, @Eui64, @LastSeenAt)
            ON DUPLICATE KEY UPDATE
              device_type = VALUES(device_type),
              assigned_bed_id = VALUES(assigned_bed_id),
              room = VALUES(room),
              status = VALUES(status),
              battery_percent = VALUES(battery_percent),
              rssi = VALUES(rssi),
              eui64 = VALUES(eui64),
              last_seen_at = VALUES(last_seen_at)
            """,
            new
            {
                device.DeviceId,
                DeviceType = device.DeviceType.ToString().ToUpperInvariant(),
                device.AssignedBedId,
                device.Room,
                Status = device.Status.ToString().ToUpperInvariant(),
                device.BatteryPercent,
                device.Rssi,
                device.Eui64,
                device.LastSeenAt
            });
    }

    public async Task<bool> DeleteAsync(string deviceId, CancellationToken cancellationToken = default)
    {
        await using var connection = await connectionFactory.OpenConnectionAsync(cancellationToken);
        var affected = await connection.ExecuteAsync(
            "DELETE FROM devices WHERE device_id = @deviceId", new { deviceId });
        return affected > 0;
    }

    private static DeviceRecord MapRow(dynamic row) => new()
    {
        DeviceId = row.device_id,
        DeviceType = Enum.Parse<DeviceType>(MapDeviceTypeName((string)row.device_type), ignoreCase: true),
        AssignedBedId = row.assigned_bed_id,
        Room = row.room,
        Status = Enum.Parse<DeviceStatus>((string)row.status, ignoreCase: true),
        BatteryPercent = row.battery_percent,
        Rssi = row.rssi,
        Eui64 = row.eui64,
        LastSeenAt = row.last_seen_at
    };

    private static string MapDeviceTypeName(string dbValue) => dbValue switch
    {
        "XG26" => nameof(DeviceType.Xg26),
        "GATEWAY" => nameof(DeviceType.Gateway),
        "BLE" => nameof(DeviceType.Ble),
        _ => dbValue
    };
}
