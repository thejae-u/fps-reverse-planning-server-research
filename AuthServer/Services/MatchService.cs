using AuthServer.Dtos;
using AuthServer.Hubs;
using AuthServer.Models;
using Microsoft.AspNetCore.SignalR;
using Microsoft.Extensions.Options;
using StackExchange.Redis;
using System.Text.Json;

namespace AuthServer.Services;

public class MatchService
{
    // DI
    private readonly IHubContext<MatchHub> _hubContext;
    private readonly ILogger<MatchService> _logger;
    private readonly IDatabase _db;
    private readonly MatchOptions _options;

    // Utilities
    private static readonly string REDIS_ENTRY_PREFIX = "match:entries"; // Match Entries
    private static readonly string REDIS_QUEUE_PREFIX = "match:queue"; // Match Waiting Queue
    private static readonly string REDIS_RESULT_PREFIX = "match:result"; // Match Result
    private static readonly string REDIS_CONNECTION_PREFIX = "match:connection"; // User Hub Connections
    private async Task<RedisValue> GetValue(string key, string field) => await _db.HashGetAsync(key, field);

    public MatchService(IHubContext<MatchHub> hubContext, ILogger<MatchService> logger, IConnectionMultiplexer redis, IOptions<MatchOptions> options)
    {
        _hubContext = hubContext;
        _logger = logger;
        _db = redis.GetDatabase();
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

                var innerTran = _db.CreateTransaction();
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
        var tran = _db.CreateTransaction();

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
        var tran = _db.CreateTransaction();

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

        var resultJson = await _db.HashGetAsync(REDIS_RESULT_PREFIX, entry.MatchId);
        if (!resultJson.HasValue)
            return null;

        return JsonSerializer.Deserialize<MatchResult>(resultJson.ToString());
    }

    public async Task TryMakeMatchesAsync()
    {
        var candidates = new List<MatchQueueEntry>();
        var poppedUserIds = new List<string>();

        while (poppedUserIds.Count < _options.PlayerCount)
        {
            // Pop From Waiting Queue (List Struct)
            var userId = await _db.ListLeftPopAsync(REDIS_QUEUE_PREFIX);
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
                await _db.ListLeftPushAsync(REDIS_QUEUE_PREFIX, userId);
            return;
        }

        var matchId = Guid.NewGuid().ToString("N");
        var result = new MatchResult
        {
            MatchId = matchId,
            MatchedAtUtc = DateTime.UtcNow,
            UserIds = candidates.Select(x => x.UserId).ToList(),
            ServerAddress = "https://logic.fps.thejaeu.com"
        };

        var matchResultJson = JsonSerializer.Serialize(result);
        var tran = _db.CreateTransaction();
        _ = tran.HashSetAsync(REDIS_RESULT_PREFIX, matchId, matchResultJson);

        foreach (var entry in candidates)
        {
            entry.Status = MatchStatus.Matched;
            entry.MatchId = matchId;
            var entryJson = JsonSerializer.Serialize(entry);
            _ = tran.HashSetAsync(REDIS_ENTRY_PREFIX, entry.UserId, entryJson);
        }

        await tran.ExecuteAsync();

        foreach (var entry in candidates)
        {
            if (entry.MatchId is null)
                throw new Exception("Entry MatchId is null");

            await _hubContext.Clients
                .Group(MatchHub.GetUserGroup(entry.UserId))
                .SendAsync("Matched", new
                {
                    UserId = entry.UserId,
                    Username = entry.Username,
                    Status = entry.Status.ToString(),
                    MatchId = entry.MatchId,
                    ServerAddress = result?.ServerAddress,
                    MatchedAtUtc = result?.MatchedAtUtc
                });

            var connectionId = await _db.HashGetAsync(REDIS_CONNECTION_PREFIX, entry.UserId);
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
    }

    public async Task RemoveEntryAsync(string userId)
    {
        var entry = await GetStatus(userId);
        if (entry is null)
            return;

        entry.Status = MatchStatus.Cancelled;
        entry.MatchId = null;
        entry.JoinedAtUtc = DateTime.MinValue;

        var tran = _db.CreateTransaction();

        _ = tran.HashSetAsync(REDIS_ENTRY_PREFIX, userId, JsonSerializer.Serialize(entry));
        _ = tran.ListRemoveAsync(REDIS_QUEUE_PREFIX, userId, 0);

        await tran.ExecuteAsync();
        _logger.LogInformation("User {userId} match entry removed due to disconnection", userId);
    }

    public async Task AddConnectionAsync(string userId, string connectionId)
    {
        var connection = await _db.HashGetAsync(REDIS_CONNECTION_PREFIX, userId);
        if (connection.HasValue)
            throw new Exception($"{userId} already exists in userConnections");

        await _db.HashSetAsync(REDIS_CONNECTION_PREFIX, userId, connectionId);
        _logger.LogInformation("{userId} add to userConnections success", userId);
    }

    public async Task RemoveConnectionAsync(string userId)
    {
        var connection = await _db.HashGetAsync(REDIS_CONNECTION_PREFIX, userId);
        if (!connection.HasValue)
            throw new Exception($"{userId}is not found in userConections");

        await _db.HashDeleteAsync(REDIS_CONNECTION_PREFIX, userId);
        _logger.LogInformation("{userId} remove from userConnections success", userId);
    }
}