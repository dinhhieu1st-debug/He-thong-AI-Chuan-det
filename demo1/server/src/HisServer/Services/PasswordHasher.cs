using System.Security.Cryptography;

namespace HisServer.Services;

/// <summary>
/// PBKDF2-HMACSHA256 password hashing.
/// </summary>
/// <remarks>
/// Stored form: <c>pbkdf2$&lt;iterations&gt;$&lt;base64 salt&gt;$&lt;base64 hash&gt;</c>.
/// The iteration count travels WITH the hash so it can be raised later without
/// locking out everyone whose password was hashed under the old cost - on the
/// next successful login the record is simply rewritten at the new cost.
///
/// PBKDF2 rather than bcrypt/argon2 because it is in the .NET base library:
/// no extra package to vet, no native dependency to break the single-file
/// deployment this server is built as.
/// </remarks>
public static class PasswordHasher
{
    private const int SaltBytes = 16;
    private const int HashBytes = 32;

    /// <summary>Cost for newly hashed passwords. Raise over time; existing
    /// hashes keep working because each carries its own count.</summary>
    public const int DefaultIterations = 210_000;

    public static string Hash(string password, int iterations = DefaultIterations)
    {
        var salt = RandomNumberGenerator.GetBytes(SaltBytes);
        var hash = Rfc2898DeriveBytes.Pbkdf2(password, salt, iterations,
                                             HashAlgorithmName.SHA256, HashBytes);
        return $"pbkdf2${iterations}${Convert.ToBase64String(salt)}${Convert.ToBase64String(hash)}";
    }

    /// <summary>
    /// Verifies a password. Returns false for any malformed record rather than
    /// throwing: a corrupt row must read as "wrong password", never as an
    /// unhandled 500 that tells an attacker the account exists.
    /// </summary>
    public static bool Verify(string password, string stored)
    {
        if (string.IsNullOrEmpty(stored)) return false;

        var parts = stored.Split('$');
        if (parts.Length != 4 || parts[0] != "pbkdf2") return false;
        if (!int.TryParse(parts[1], out var iterations) || iterations <= 0) return false;

        byte[] salt, expected;
        try
        {
            salt = Convert.FromBase64String(parts[2]);
            expected = Convert.FromBase64String(parts[3]);
        }
        catch (FormatException)
        {
            return false;
        }

        var actual = Rfc2898DeriveBytes.Pbkdf2(password, salt, iterations,
                                               HashAlgorithmName.SHA256, expected.Length);
        // Fixed-time comparison: a plain != would leak how many leading bytes
        // matched through its timing.
        return CryptographicOperations.FixedTimeEquals(actual, expected);
    }
}
