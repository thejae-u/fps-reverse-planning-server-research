namespace AuthServer.Models;

public enum MatchStatus
{
    Waiting,
    Matched,
    Cancelled,
    Completed,
    Failed
}

public class MatchQueueEntry
{
    public string UserId { get; set; } = string.Empty;
    public string Username { get; set; } = string.Empty;
    public MatchStatus Status { get; set; } = MatchStatus.Waiting;
    public DateTime JoinedAtUtc { get; set; } = DateTime.UtcNow;
    public string? MatchId { get; set; }
}