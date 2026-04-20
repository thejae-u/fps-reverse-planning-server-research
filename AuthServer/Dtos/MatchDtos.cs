namespace AuthServer.Dtos;

public class MatchJoinResponse
{
    public string UserId { get; set; } = string.Empty;
    public string Username { get; set; } = string.Empty;
    public string Status { get; set; } = string.Empty;
    public DateTime JoinedAtUtc { get; set; }
}

public class MatchStatusResponse
{
    public string UserId { get; set; } = string.Empty;
    public string Username { get; set; } = string.Empty;
    public string Status { get; set; } = string.Empty;
    public DateTime JoinedAtUtc { get; set; }
    public string? MatchId { get; set; }
    public string? ServerAddress { get; set; }
}