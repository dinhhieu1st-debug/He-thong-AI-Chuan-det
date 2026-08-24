using Dapper;

namespace HisServer.Data;

public sealed class FcmTokenRepository
{
    private readonly DbConnectionFactory connectionFactory;

    public FcmTokenRepository(DbConnectionFactory connectionFactory)
    {
        this.connectionFactory = connectionFactory;
    }

    public async Task<IReadOnlyList<string>> GetAllTokensAsync(CancellationToken cancellationToken = default)
    {
        await using var connection = await connectionFactory.OpenConnectionAsync(cancellationToken);
        var tokens = await connection.QueryAsync<string>("SELECT token FROM fcm_tokens");
        return tokens.ToList();
    }

    public async Task RegisterAsync(string token, CancellationToken cancellationToken = default)
    {
        await using var connection = await connectionFactory.OpenConnectionAsync(cancellationToken);
        await connection.ExecuteAsync(
            """
            INSERT INTO fcm_tokens (token, registered_at)
            VALUES (@token, NOW())
            ON DUPLICATE KEY UPDATE last_used_at = last_used_at
            """,
            new { token });
    }

    public async Task RemoveAsync(string token, CancellationToken cancellationToken = default)
    {
        await using var connection = await connectionFactory.OpenConnectionAsync(cancellationToken);
        await connection.ExecuteAsync("DELETE FROM fcm_tokens WHERE token = @token", new { token });
    }

    public async Task TouchLastUsedAsync(IEnumerable<string> tokens, CancellationToken cancellationToken = default)
    {
        await using var connection = await connectionFactory.OpenConnectionAsync(cancellationToken);
        await connection.ExecuteAsync(
            "UPDATE fcm_tokens SET last_used_at = NOW() WHERE token = @token",
            tokens.Select(t => new { token = t }));
    }
}
