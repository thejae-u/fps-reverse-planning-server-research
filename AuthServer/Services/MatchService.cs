using AuthServer.Data;
using AuthServer.Dtos;
using AuthServer.Hubs;
using AuthServer.Models;
using Microsoft.AspNetCore.SignalR;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Options;
using StackExchange.Redis;
using System.Text.Json;

namespace AuthServer.Services;

public class MatchService
{
    // DI
    private readonly IHubContext<MatchHub> _hubContext;
    private readonly ILogger<MatchService> _logger;
    private readonly IDatabase _redisDB;
    private readonly IServiceScopeFactory _scopeFactory;
    private readonly MatchOptions _options;

    // Utilities
    private static readonly string REDIS_ENTRY_PREFIX = "match:entries"; // Match Entries
    private static readonly string REDIS_QUEUE_PREFIX = "match:queue"; // Match Waiting Queue
    private static readonly string REDIS_RESULT_PREFIX = "match:result"; // Match Result
    private static readonly string REDIS_CONNECTION_PREFIX = "match:connection"; // User Hub Connections
    private async Task<RedisValue> GetValue(string key, string field) => await _redisDB.HashGetAsync(key, field);

    public MatchService(IHubContext<MatchHub> hubContext, ILogger<MatchService> logger, IConnectionMultiplexer redis, IServiceScopeFactory scopeFactory, IOptions<MatchOptions> options)
    {
        _hubContext = hubContext;
        _logger = logger;
        _redisDB = redis.GetDatabase();
        _scopeFactory = scopeFactory;
        _options = options.Value;
    }

    public async Task<Result<MatchQueueEntry>> Join(string userId, string username)
    {
        var existingJson = await GetValue(REDIS_ENTRY_PREFIX, userId);
        if(existingJson.HasValue)
        {
            var existing = JsonSerializer.Deserialize<MatchQueueEntry>(existingJson.ToString());

            if(existing is not null)
            {
                if (existing.Status == MatchStatus.Waiting)
                    return Result<MatchQueueEntry>.Failure("ALREADY_IN_QUEUE", "이미 매칭 큐에 참가 중입니다.");

                if (existing.Status == MatchStatus.Matched)
                    return Result<MatchQueueEntry>.Failure("ALREADY_MATCHED", "이미 매칭이 완료되었습니다.");

                // Update Entry
                existing.Status = MatchStatus.Waiting;
                existing.JoinedAtUtc = DateTime.UtcNow;
                existing.MatchId = null;
                existing.Username = username;

                // Redis Update
                var updatedJson = JsonSerializer.Serialize(existing);

                var innerTran = _redisDB.CreateTransaction();
                _ = innerTran.HashSetAsync(REDIS_ENTRY_PREFIX, userId, updatedJson);
                _ = innerTran.ListRightPushAsync(REDIS_QUEUE_PREFIX, userId);

                bool innerCommited = await innerTran.ExecuteAsync();
                return innerCommited ? Result<MatchQueueEntry>.Success(existing) : Result<MatchQueueEntry>.Failure("INTERNAL_ERROR", "서버 DB 오류");
            }

            // if json deserialize failed -> internal server error
            return Result<MatchQueueEntry>.Failure("INTERNAL_SEVER_ERROR", "서버 JSON 오류");
        }

        var entry = new MatchQueueEntry
        {
            UserId = userId,
            Username = username,
            Status = MatchStatus.Waiting,
            JoinedAtUtc = DateTime.UtcNow
        };

        var json = JsonSerializer.Serialize(entry);
        var tran = _redisDB.CreateTransaction();

        _ = tran.HashSetAsync(REDIS_ENTRY_PREFIX, userId, json);
        _ = tran.ListRightPushAsync(REDIS_QUEUE_PREFIX, userId);

        bool commited = await tran.ExecuteAsync();
        return commited ? Result<MatchQueueEntry>.Success(entry) : Result<MatchQueueEntry>.Failure("INTERNAL_SEVER_ERROR", "서버 DB 오류");
    }

    public async Task<Result<MatchQueueEntry>> Cancel(string userId)
    {
        var existingJson = await GetValue(REDIS_ENTRY_PREFIX, userId);
        if(!existingJson.HasValue)
            return Result<MatchQueueEntry>.Failure("QUEUE_NOT_FOUND", "매칭 큐 정보가 없습니다.");

        // Check valid
        var entry = JsonSerializer.Deserialize<MatchQueueEntry>(existingJson.ToString());
        if (entry is null)
            return Result<MatchQueueEntry>.Failure("INTERNAL_SERVER_ERROR", "서버 JSON 오류");

        if (entry.Status != MatchStatus.Waiting)
            return Result<MatchQueueEntry>.Failure("NOT_WAITING", "현재 대기 상태가 아닙니다.");

        // Entry Update
        entry.Status = MatchStatus.Cancelled;

        // Redis Update
        var json = JsonSerializer.Serialize(entry);
        var tran = _redisDB.CreateTransaction();

        _ = tran.HashSetAsync(REDIS_ENTRY_PREFIX, userId, json);
        _ = tran.ListRemoveAsync(REDIS_QUEUE_PREFIX, userId, 1);

        bool commited = await tran.ExecuteAsync();
        return commited ? Result<MatchQueueEntry>.Success(entry) : Result<MatchQueueEntry>.Failure("INTERNAL_SERVER_ERROR", "서버 DB 오류");
    }

    public async Task<MatchQueueEntry?> GetStatus(string userId)
    {
        var existingJson = await GetValue(REDIS_ENTRY_PREFIX, userId);
        if (!existingJson.HasValue)
            return null;

        return JsonSerializer.Deserialize<MatchQueueEntry>(existingJson.ToString());
    }

    public async Task<MatchResult?> GetMatchResultByUserId(string userId)
    {
        var entry = await GetStatus(userId);
        if (entry is null || string.IsNullOrEmpty(entry.MatchId))
            return null;

        var resultJson = await _redisDB.HashGetAsync(REDIS_RESULT_PREFIX, entry.MatchId);
        if (!resultJson.HasValue)
            return null;

        return JsonSerializer.Deserialize<MatchResult>(resultJson.ToString());
    }

    public async Task<MatchResult?> TryMakeMatchesAsync()
    {
        var candidates = new List<MatchQueueEntry>();
        var poppedUserIds = new List<string>();

        while (poppedUserIds.Count < _options.PlayerCount)
        {
            // Pop From Waiting Queue (List Struct)
            var userId = await _redisDB.ListLeftPopAsync(REDIS_QUEUE_PREFIX);
            if (userId.IsNull) break;

            // User Status Check
            var entry = await GetStatus(userId!);
            if (entry is not null && entry.Status == MatchStatus.Waiting)
            {
                candidates.Add(entry);
                poppedUserIds.Add(userId!);
            }
        }

        // Unreached Required Count
        if (poppedUserIds.Count < _options.PlayerCount)
        {
            foreach (var userId in poppedUserIds)
                await _redisDB.ListLeftPushAsync(REDIS_QUEUE_PREFIX, userId);
            return null;
        }

        // make new match information
        var matchId = Guid.NewGuid().ToString("N");
        var result = new MatchResult
        {
            MatchId = matchId,
            MatchedAtUtc = DateTime.UtcNow,
            UserIds = candidates.Select(x => x.UserId).ToList(),
            ServerAddress = "pending"
        };

        // Redis Transaction execute
        var matchResultJson = JsonSerializer.Serialize(result);
        var tran = _redisDB.CreateTransaction();
        _ = tran.HashSetAsync(REDIS_RESULT_PREFIX, matchId, matchResultJson);

        foreach (var entry in candidates)
        {
            entry.Status = MatchStatus.Matched;
            entry.MatchId = matchId;
            var entryJson = JsonSerializer.Serialize(entry);
            _ = tran.HashSetAsync(REDIS_ENTRY_PREFIX, entry.UserId, entryJson);
        }

        await tran.ExecuteAsync();

        // SignalR Hub Send
        foreach (var entry in candidates)
        {
            if (entry.MatchId is null)
                throw new Exception("Entry MatchId is null");

            var connectionId = await _redisDB.HashGetAsync(REDIS_CONNECTION_PREFIX, entry.UserId);
            if (connectionId.HasValue)
            {
                var matchGroup = MatchHub.GetMatchGroup(entry.MatchId);
                var connectionIdStr = connectionId.ToString();
                await _hubContext.Groups.AddToGroupAsync(connectionIdStr, matchGroup);
                await _hubContext.Clients.Client(connectionIdStr).SendAsync("JoinedMatchChat", new
                {
                    matchId = entry.MatchId,
                    connectionId = connectionIdStr
                });

                await _hubContext.Clients.Group(matchGroup).SendAsync("SystemMessage", new
                {
                    matchId = entry.MatchId,
                    message = $"{entry.Username} 님이 입장했습니다.",
                    sendAt = DateTimeOffset.UtcNow
                });
            }
        }

        return result;
    }

    public async Task UpdateMatchServerAddressAsync(string matchId, string serverAddress)
    {
        var resultJson = await _redisDB.HashGetAsync(REDIS_RESULT_PREFIX, matchId);
        if (!resultJson.HasValue) return;

        var result = JsonSerializer.Deserialize<MatchResult>(resultJson.ToString());
        if (result is null) return;

        result.ServerAddress = serverAddress;
        await _redisDB.HashSetAsync(REDIS_RESULT_PREFIX, matchId, JsonSerializer.Serialize(result));
    }

    public async Task RemoveEntryAsync(string userId)
    {
        var entry = await GetStatus(userId);
        if (entry is null)
            return;

        entry.Status = MatchStatus.Cancelled;
        entry.MatchId = null;
        entry.JoinedAtUtc = DateTime.MinValue;

        var tran = _redisDB.CreateTransaction();

        _ = tran.HashSetAsync(REDIS_ENTRY_PREFIX, userId, JsonSerializer.Serialize(entry));
        _ = tran.ListRemoveAsync(REDIS_QUEUE_PREFIX, userId, 0);

        await tran.ExecuteAsync();
        _logger.LogInformation("User {userId} match entry removed due to disconnection", userId);
    }

    public async Task AddConnectionAsync(string userId, string connectionId)
    {
        var connection = await _redisDB.HashGetAsync(REDIS_CONNECTION_PREFIX, userId);
        if (connection.HasValue)
            throw new Exception($"{userId} already exists in userConnections");

        await _redisDB.HashSetAsync(REDIS_CONNECTION_PREFIX, userId, connectionId);
        _logger.LogInformation("{userId} add to userConnections success", userId);
    }

    public async Task RemoveConnectionAsync(string userId)
    {
        var connection = await _redisDB.HashGetAsync(REDIS_CONNECTION_PREFIX, userId);
        if (!connection.HasValue)
            throw new Exception($"{userId}is not found in userConections");

        await _redisDB.HashDeleteAsync(REDIS_CONNECTION_PREFIX, userId);
        _logger.LogInformation("{userId} remove from userConnections success", userId);
    }

    public async Task<bool> FinishMatchAsync(string matchId, TeamSide winningTeam, List<string> winnerUserIds)
    {
        return await FinishMatchAsync(new GameResultReportDto
        {
            MatchId = matchId,
            WinningTeam = winningTeam,
            WinnerUserIds = winnerUserIds,
            EndTimeUtc = DateTime.UtcNow
        });
    }

    public async Task<bool> FinishMatchAsync(GameResultReportDto report)
    {
        // 1. Redis에서 진행 중인 매치 정보 조회
        var matchJson = await _redisDB.HashGetAsync(REDIS_RESULT_PREFIX, report.MatchId);
        if (!matchJson.HasValue)
        {
            _logger.LogWarning("Match {matchId} not found in Redis (already finished or invalid)", report.MatchId);
            return false;
        }

        var redisMatch = JsonSerializer.Deserialize<MatchResult>(matchJson.ToString());
        if (redisMatch is null)
        {
            _logger.LogError("Failed to deserialize Redis MatchResult for match {matchId}", report.MatchId);
            return false;
        }

        // 2. PostgreSQL DB 영구 저장 (Scope)
        using var scope = _scopeFactory.CreateScope();
        var dbContext = scope.ServiceProvider.GetRequiredService<ApplicationDbContext>();

        var existingInDb = await dbContext.MatchResults.FindAsync(report.MatchId);
        var finishedAt = report.EndTimeUtc != default ? report.EndTimeUtc : DateTime.UtcNow;

        if (existingInDb is null)
        {
            var matchEntity = new MatchResult
            {
                MatchId = report.MatchId,
                UserIds = redisMatch.UserIds,
                ServerAddress = redisMatch.ServerAddress,
                IsFinished = true,
                WinningTeam = report.WinningTeam.ToString(),
                WinnerUserIds = report.WinnerUserIds,
                TeamAScore = report.TeamAScore,
                TeamBScore = report.TeamBScore,
                MatchedAtUtc = redisMatch.MatchedAtUtc,
                FinishedAtUtc = finishedAt
            };
            await dbContext.MatchResults.AddAsync(matchEntity);
        }
        else
        {
            if (existingInDb.IsFinished)
            {
                _logger.LogWarning("Match {matchId} is already marked as finished in DB", report.MatchId);
                return false;
            }

            existingInDb.IsFinished = true;
            existingInDb.WinningTeam = report.WinningTeam.ToString();
            existingInDb.WinnerUserIds = report.WinnerUserIds;
            existingInDb.TeamAScore = report.TeamAScore;
            existingInDb.TeamBScore = report.TeamBScore;
            existingInDb.FinishedAtUtc = finishedAt;
        }

        await dbContext.SaveChangesAsync();

        // 3. Redis 캐시 및 유저 엔트리 정리 (Transaction)
        var tran = _redisDB.CreateTransaction();
        _ = tran.HashDeleteAsync(REDIS_RESULT_PREFIX, report.MatchId);

        foreach (var userId in redisMatch.UserIds)
        {
            _ = tran.HashDeleteAsync(REDIS_ENTRY_PREFIX, userId);
        }

        bool committed = await tran.ExecuteAsync();
        if (!committed)
        {
            _logger.LogWarning("Redis transaction failed during match {matchId} cleanup", report.MatchId);
        }

        _logger.LogInformation("Match {matchId} finished successfully: WinningTeam={winningTeam}, Score={a}:{b}, FinishedAt={dateTime}",
            report.MatchId, report.WinningTeam, report.TeamAScore, report.TeamBScore, finishedAt.ToString("O"));

        // 4. SignalR 클라이언트들에게 최종 게임 결과 브로드캐스트
        var matchGroup = MatchHub.GetMatchGroup(report.MatchId);
        await _hubContext.Clients.Group(matchGroup).SendAsync("GameFinished", new
        {
            matchId = report.MatchId,
            winningTeam = report.WinningTeam.ToString(),
            winnerUserIds = report.WinnerUserIds,
            teamAScore = report.TeamAScore,
            teamBScore = report.TeamBScore,
            playerStats = report.PlayerStats
        });

        return true;
    }
}