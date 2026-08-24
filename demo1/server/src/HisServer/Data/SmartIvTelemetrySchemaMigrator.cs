using Dapper;

namespace HisServer.Data;

public sealed class SmartIvTelemetrySchemaMigrator
{
    private static readonly (string Name, string Definition)[] Columns =
    {
        ("hr_forecast_16s", "INT NULL"),
        ("spo2_forecast_16s", "INT NULL"),
        ("hr_trend_bpm_per_min", "INT NULL"),
        ("ts_anomaly_score_x100", "INT NULL"),
        ("drops_forecast_16s", "INT NULL"),
        ("drops_trend_dpm_per_min", "INT NULL"),
        ("drops_forecast_trusted", "TINYINT(1) NOT NULL DEFAULT 0"),
        ("line_state", "INT NULL"),
        ("remaining_ml", "INT NULL"),
        ("remaining_min", "INT NULL"),
        ("drop_interval_ms", "INT NULL"),
        ("drop_event_count", "INT NULL"),
        ("server_drop_level", "INT NULL"),
        ("vitals_level", "INT NULL"),
        ("alert_level", "INT NULL"),
        ("vitals_test_mode", "INT NULL"),
        ("ai_input_heart_rate", "INT NULL"),
        ("ai_input_spo2", "INT NULL")
    };

    private readonly DbConnectionFactory connectionFactory;

    public SmartIvTelemetrySchemaMigrator(DbConnectionFactory connectionFactory)
    {
        this.connectionFactory = connectionFactory;
    }

    public async Task ApplyAsync(CancellationToken cancellationToken = default)
    {
        await using var connection = await connectionFactory.OpenConnectionAsync(cancellationToken);
        foreach (var (name, definition) in Columns)
        {
            var exists = await connection.ExecuteScalarAsync<int>(
                "SELECT COUNT(*) FROM information_schema.COLUMNS " +
                "WHERE TABLE_SCHEMA=DATABASE() AND TABLE_NAME='vital_samples' AND COLUMN_NAME=@name",
                new { name });
            if (exists == 0)
            {
                // Both identifiers and definitions come exclusively from the
                // constant table above; no request data enters this DDL.
                await connection.ExecuteAsync($"ALTER TABLE vital_samples ADD COLUMN `{name}` {definition}");
            }
        }
    }
}
