namespace HisServer.Models;

public enum UserRole
{
    Nurse,
    Technician,
    Admin
}

public sealed class UserRecord
{
    public int UserId { get; init; }
    public required string Username { get; init; }
    public string PasswordHash { get; set; } = string.Empty;
    public string FullName { get; set; } = string.Empty;
    public UserRole Role { get; set; } = UserRole.Nurse;
    public bool IsActive { get; set; } = true;
    public bool MustChangePassword { get; set; }
    public DateTime CreatedAt { get; init; }
    public DateTime? LastLoginAt { get; set; }
}

public enum AssignmentScope
{
    Room,
    Bed
}

/// <summary>One line of a user's duty roster: a whole room, or a single bed.</summary>
public sealed class UserAssignment
{
    public int AssignmentId { get; init; }
    public int UserId { get; init; }
    public AssignmentScope ScopeType { get; init; }
    public required string ScopeValue { get; init; }
    public DateTime AssignedAt { get; init; }
}
