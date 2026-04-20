namespace AuthServer.Models;

public class MatchResult
{
    public string MatchId { get; set; } = Guid.NewGuid().ToString("N");
    public DateTime MatchedAtUtc { get; set; } = DateTime.UtcNow;
    public List<string> UserIds { get; set; } = new();
    public string ServerAddress { get; set; } = "pending";
}