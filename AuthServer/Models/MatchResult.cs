using System.ComponentModel.DataAnnotations;

namespace AuthServer.Models;

public class MatchResult
{
    [Key]
    public string MatchId { get; set; } = Guid.NewGuid().ToString("N");
    
    // 전체 유저 ID
    public List<string> UserIds { get; set; } = new();
    public string ServerAddress { get; set; } = "pending";

    public bool IsFinished { get; set; } = false;
    
    // 5 vs 5
    public string? WinningTeam { get; set; } // "A", "B", "Draw"
    public List<string> WinnerUserIds { get; set; } = new();
    
    public DateTime MatchedAtUtc { get; set; } = DateTime.UtcNow;
    public DateTime? FinishedAtUtc { get; set; }
}