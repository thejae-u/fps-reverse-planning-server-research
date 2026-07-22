namespace AuthServer.Dtos;

public enum TeamSide
{
    None = 0,
    TeamA = 1,
    TeamB = 2,
    Draw = 3
}

public class PlayerMatchStatDto
{
    public TeamSide Team { get; set; }
    public string UserId { get; set; } = string.Empty;
    public int Kills { get; set; }
    public int Assists { get; set; }
    public int Deaths { get; set; }
    public int Damage { get; set; }
    public int Heals { get; set; }
    public int Guards { get; set; }
}

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

public class GameResultReportDto
{
    public string MatchId { get; set; } = string.Empty;
    public TeamSide WinningTeam { get; set; }
    public int TeamAScore { get; set; }
    public int TeamBScore { get; set; }
    public List<string> WinnerUserIds { get; set; } = new();
    public List<PlayerMatchStatDto> PlayerStats { get; set; } = new();
    public DateTime EndTimeUtc { get; set; }
    public string ApiKey { get; set; } = string.Empty;
}