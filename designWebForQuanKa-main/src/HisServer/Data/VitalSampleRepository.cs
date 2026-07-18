using Dapper;
using HisServer.Domain;

namespace HisServer.Data;

public sealed class VitalSampleRepository
{
    private readonly DbConnectionFactory connectionFactory;

    public VitalSampleRepository(DbConnectionFactory connectionFactory)
    {
        this.connectionFactory = connectionFactory;
    }

    public async Task SaveAsync(
        string bedId,
        string? deviceId,
        int spo2,
        int heartRate,
        double temperature,
        int dripRate,
        BedStatus status,
        DateTime recordedAt,
        CancellationToken cancellationToken = default)
    {
        await using var connection = await connectionFactory.OpenConnectionAsync(cancellationToken);
        await connection.ExecuteAsync(
            """
            INSERT INTO vital_samples (bed_id, device_id, spo2, heart_rate, temperature, drip_rate, status, recorded_at)
            VALUES (@bedId, @deviceId, @spo2, @heartRate, @temperature, @dripRate, @status, @recordedAt)
            """,
            new
            {
                bedId,
                deviceId,
                spo2,
                heartRate,
                temperature,
                dripRate,
                status = status.ToString().ToUpperInvariant(),
                recordedAt
            });
    }
}
