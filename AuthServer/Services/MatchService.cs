using AuthServer.Hubs;
using AuthServer.Models;
using Microsoft.AspNetCore.SignalR;
using Microsoft.Extensions.Options;

namespace AuthServer.Services;

public class MatchService
{
    private readonly Dictionary<string, MatchQueueEntry> _entries = new();
    private readonly Queue<string> _waitingQueue = new();
    private readonly Dictionary<string, MatchResult> _matchResults = new();
    private readonly object _lock = new();
    private readonly IHubContext<MatchHub> _hubContext;

    private readonly MatchOptions _options;

    public MatchService(IHubContext<MatchHub> hubContext, IOptions<MatchOptions> options)
    {
        _hubContext = hubContext;
        _options = options.Value;
    }

    public Result<MatchQueueEntry> Join(string userId, string username)
    {
        lock (_lock)
        {
            if (_entries.TryGetValue(userId, out var existing))
            {
                if (existing.Status == MatchStatus.Waiting)
                {
                    return Result<MatchQueueEntry>.Failure("ALREADY_IN_QUEUE", "이미 매칭 큐에 참가 중입니다.");
                }

                if (existing.Status == MatchStatus.Matched)
                {
                    return Result<MatchQueueEntry>.Failure("ALREADY_MATCHED", "이미 매칭이 완료되었습니다.");
                }

                existing.Status = MatchStatus.Waiting;
                existing.JoinedAtUtc = DateTime.UtcNow;
                existing.MatchId = null;
                existing.Username = username;

                _waitingQueue.Enqueue(userId);
                return Result<MatchQueueEntry>.Success(existing);
            }

            var entry = new MatchQueueEntry
            {
                UserId = userId,
                Username = username,
                Status = MatchStatus.Waiting,
                JoinedAtUtc = DateTime.UtcNow
            };

            _entries[userId] = entry;
            _waitingQueue.Enqueue(userId);

            return Result<MatchQueueEntry>.Success(entry);
        }
    }

    public Result<MatchQueueEntry> Cancel(string userId)
    {
        lock (_lock)
        {
            if (!_entries.TryGetValue(userId, out var entry))
            {
                return Result<MatchQueueEntry>.Failure("QUEUE_NOT_FOUND", "매칭 큐 정보가 없습니다.");
            }

            if (entry.Status != MatchStatus.Waiting)
            {
                return Result<MatchQueueEntry>.Failure("NOT_WAITING", "현재 대기 상태가 아닙니다.");
            }

            entry.Status = MatchStatus.Cancelled;
            return Result<MatchQueueEntry>.Success(entry);
        }
    }

    public MatchQueueEntry? GetStatus(string userId)
    {
        lock (_lock)
        {
            _entries.TryGetValue(userId, out var entry);
            return entry;
        }
    }

    public MatchResult? GetMatchResultByUserId(string userId)
    {
        lock (_lock)
        {
            if (!_entries.TryGetValue(userId, out var entry))
                return null;

            if (string.IsNullOrEmpty(entry.MatchId))
                return null;

            _matchResults.TryGetValue(entry.MatchId, out var result);
            return result;
        }
    }

    public async Task TryMakeMatchesAsync()
    {
        List<MatchQueueEntry> candidates;
        MatchResult? result = null;

        lock (_lock)
        {
            candidates = new List<MatchQueueEntry>();

            while (_waitingQueue.Count > 0 && candidates.Count < _options.PlayerCount)
            {
                var userId = _waitingQueue.Dequeue();

                if (!_entries.TryGetValue(userId, out var entry))
                    continue;

                if (entry.Status != MatchStatus.Waiting)
                    continue;

                candidates.Add(entry);
            }

            if (candidates.Count < _options.PlayerCount)
            {
                foreach (var candidate in candidates)
                    _waitingQueue.Enqueue(candidate.UserId);

                return;
            }

            var matchId = Guid.NewGuid().ToString("N");

            result = new MatchResult
            {
                MatchId = matchId,
                MatchedAtUtc = DateTime.UtcNow,
                UserIds = candidates.Select(x => x.UserId).ToList(),
                ServerAddress = "https://logic.fps.thejaeu.com"
            };

            _matchResults[matchId] = result;

            foreach (var entry in candidates)
            {
                entry.Status = MatchStatus.Matched;
                entry.MatchId = matchId;
            }
        }

        foreach (var entry in candidates)
        {
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
        }
    }
}