using System.ComponentModel.DataAnnotations;

namespace AuthServer.Models;

public class MatchResult
{
    [Key]
    public string MatchId { get; set; } = Guid.NewGuid().ToString("N");
    public DateTime MatchedAtUtc { get; set; } = DateTime.UtcNow;
    public List<string> UserIds { get; set; } = new();
    public string ServerAddress { get; set; } = "pending";

    public bool IsFinished { get; set; } = false;
    public string? WinnerId { get; set; }
    public DateTime? FinishedAtUtc { get; set; }
}