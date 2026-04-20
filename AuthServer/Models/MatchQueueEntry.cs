namespace AuthServer.Models;

public enum MatchStatus
{
    Waiting,
    Matched,
    Cancelled
}

public class MatchQueueEntry
{
    public string UserId { get; set; } = string.Empty;
    public string Username { get; set; } = string.Empty;
    public MatchStatus Status { get; set; } = MatchStatus.Waiting;
    public DateTime JoinedAtUtc { get; set; } = DateTime.UtcNow;
}