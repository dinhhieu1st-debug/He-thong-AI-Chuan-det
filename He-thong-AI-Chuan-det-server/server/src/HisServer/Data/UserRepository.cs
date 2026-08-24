using Dapper;
using HisServer.Models;

namespace HisServer.Data;

public sealed class UserRepository
{
    private readonly DbConnectionFactory connectionFactory;

    public UserRepository(DbConnectionFactory connectionFactory)
    {
        this.connectionFactory = connectionFactory;
    }

    private const string Columns =
        "user_id, username, password_hash, full_name, role, is_active, " +
        "must_change_password, created_at, last_login_at";

    private static UserRecord MapRow(dynamic row) => new()
    {
        UserId = (int)row.user_id,
        Username = (string)row.username,
        PasswordHash = (string)row.password_hash,
        FullName = (string)row.full_name,
        Role = Enum.Parse<UserRole>((string)row.role, ignoreCase: true),
        IsActive = (bool)row.is_active,
        MustChangePassword = (bool)row.must_change_password,
        CreatedAt = (DateTime)row.created_at,
        LastLoginAt = row.last_login_at is null ? null : (DateTime?)row.last_login_at
    };

    public async Task<UserRecord?> FindByUsernameAsync(string username, CancellationToken ct = default)
    {
        await using var connection = await connectionFactory.OpenConnectionAsync(ct);
        var row = await connection.QuerySingleOrDefaultAsync(
            $"SELECT {Columns} FROM users WHERE username = @username", new { username });
        return row is null ? null : MapRow(row);
    }

    public async Task<UserRecord?> FindByIdAsync(int userId, CancellationToken ct = default)
    {
        await using var connection = await connectionFactory.OpenConnectionAsync(ct);
        var row = await connection.QuerySingleOrDefaultAsync(
            $"SELECT {Columns} FROM users WHERE user_id = @userId", new { userId });
        return row is null ? null : MapRow(row);
    }

    public async Task<IReadOnlyList<UserRecord>> GetAllAsync(CancellationToken ct = default)
    {
        await using var connection = await connectionFactory.OpenConnectionAsync(ct);
        var rows = await connection.QueryAsync($"SELECT {Columns} FROM users ORDER BY username");
        return rows.Select(r => (UserRecord)MapRow(r)).ToList();
    }

    public async Task<int> CreateAsync(string username, string passwordHash, string fullName,
                                       UserRole role, CancellationToken ct = default)
    {
        await using var connection = await connectionFactory.OpenConnectionAsync(ct);
        return await connection.ExecuteScalarAsync<int>(
            """
            INSERT INTO users (username, password_hash, full_name, role, must_change_password)
            VALUES (@username, @passwordHash, @fullName, @role, TRUE);
            SELECT LAST_INSERT_ID();
            """,
            new { username, passwordHash, fullName, role = role.ToString().ToUpperInvariant() });
    }

    public async Task UpdateAsync(int userId, string fullName, UserRole role, bool isActive,
                                  CancellationToken ct = default)
    {
        await using var connection = await connectionFactory.OpenConnectionAsync(ct);
        await connection.ExecuteAsync(
            """
            UPDATE users SET full_name = @fullName, role = @role, is_active = @isActive
            WHERE user_id = @userId
            """,
            new { userId, fullName, role = role.ToString().ToUpperInvariant(), isActive });
    }

    /// <summary>Sets a new password. <paramref name="mustChange"/> is true when an
    /// ADMIN reset it (the owner has not chosen it yet), false when the owner did.</summary>
    public async Task SetPasswordAsync(int userId, string passwordHash, bool mustChange,
                                       CancellationToken ct = default)
    {
        await using var connection = await connectionFactory.OpenConnectionAsync(ct);
        await connection.ExecuteAsync(
            "UPDATE users SET password_hash = @passwordHash, must_change_password = @mustChange " +
            "WHERE user_id = @userId",
            new { userId, passwordHash, mustChange });
    }

    public async Task TouchLoginAsync(int userId, CancellationToken ct = default)
    {
        await using var connection = await connectionFactory.OpenConnectionAsync(ct);
        await connection.ExecuteAsync(
            "UPDATE users SET last_login_at = UTC_TIMESTAMP(3) WHERE user_id = @userId",
            new { userId });
    }

    public async Task DeleteAsync(int userId, CancellationToken ct = default)
    {
        await using var connection = await connectionFactory.OpenConnectionAsync(ct);
        await connection.ExecuteAsync("DELETE FROM users WHERE user_id = @userId", new { userId });
    }

    // ---- Duty roster -------------------------------------------------------

    public async Task<IReadOnlyList<UserAssignment>> GetAssignmentsAsync(int userId,
                                                                        CancellationToken ct = default)
    {
        await using var connection = await connectionFactory.OpenConnectionAsync(ct);
        var rows = await connection.QueryAsync(
            "SELECT assignment_id, user_id, scope_type, scope_value, assigned_at " +
            "FROM user_assignments WHERE user_id = @userId ORDER BY scope_type, scope_value",
            new { userId });

        return rows.Select(r => new UserAssignment
        {
            AssignmentId = (int)r.assignment_id,
            UserId = (int)r.user_id,
            ScopeType = Enum.Parse<AssignmentScope>((string)r.scope_type, ignoreCase: true),
            ScopeValue = (string)r.scope_value,
            AssignedAt = (DateTime)r.assigned_at
        }).ToList();
    }

    public async Task AddAssignmentAsync(int userId, AssignmentScope scope, string value,
                                         CancellationToken ct = default)
    {
        await using var connection = await connectionFactory.OpenConnectionAsync(ct);
        // Assigning the same room twice is a double-click, not an error.
        await connection.ExecuteAsync(
            """
            INSERT INTO user_assignments (user_id, scope_type, scope_value)
            VALUES (@userId, @scope, @value)
            ON DUPLICATE KEY UPDATE assigned_at = CURRENT_TIMESTAMP
            """,
            new { userId, scope = scope.ToString().ToUpperInvariant(), value });
    }

    public async Task RemoveAssignmentAsync(int assignmentId, CancellationToken ct = default)
    {
        await using var connection = await connectionFactory.OpenConnectionAsync(ct);
        await connection.ExecuteAsync(
            "DELETE FROM user_assignments WHERE assignment_id = @assignmentId", new { assignmentId });
    }
}
