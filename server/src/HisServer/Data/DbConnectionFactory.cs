using MySqlConnector;

namespace HisServer.Data;

/// <summary>
/// Opens MySQL connections from a single configured connection string.
/// Unlike the old app, there is no guessed-password fallback chain — if the
/// connection string is missing, the app fails fast at startup with a clear error.
/// </summary>
public sealed class DbConnectionFactory
{
    private readonly string connectionString;

    public DbConnectionFactory(IConfiguration configuration)
    {
        connectionString = configuration.GetConnectionString("MySql") ?? string.Empty;
        if (string.IsNullOrWhiteSpace(connectionString))
        {
            throw new InvalidOperationException(
                "ConnectionStrings:MySql is not configured. Set it via appsettings.json, " +
                "the ConnectionStrings__MySql environment variable, or dotnet user-secrets.");
        }
    }

    public async Task<MySqlConnection> OpenConnectionAsync(CancellationToken cancellationToken = default)
    {
        var connection = new MySqlConnection(connectionString);
        await connection.OpenAsync(cancellationToken);
        return connection;
    }
}
