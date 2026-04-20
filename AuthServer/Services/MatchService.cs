using AuthServer.Models;
using AuthServer.Dtos;
using System.Collections.Concurrent;
using Microsoft.AspNetCore.Mvc.ModelBinding.Binders;

namespace AuthServer.Services;

public class MatchService
{
    private readonly Dictionary<string, MatchQueueEntry> _entries = new();
    private readonly Queue<string> _waitingQueue = new();
    private readonly Dictionary<string, MatchResult> _matchResults = new();
    private readonly ConcurrentDictionary<string, TaskCompletionSource<MatchStatusResponse>> _waiters = new();
    private readonly object _lock = new();

    private readonly GlobalFields _globalFields;

    public MatchService(GlobalFields globalFields)
    {
        _globalFields = globalFields;
    }

    public async Task<MatchStatusResponse?> WaitForMatchAsync(string userId, CancellationToken cancellationToken)
    {
        var current = GetStatus(userId);
        if (current is null)
            return null;

        if (current.Status == MatchStatus.Matched)
        {
            var matched = BuildStatusResponse(current);
            return matched;
        }

        var tcs = new TaskCompletionSource<MatchStatusResponse>(TaskCreationOptions.RunContinuationsAsynchronously);
        _waiters.AddOrUpdate(userId, tcs, (_, __) => tcs);

        using var registration = cancellationToken.Register(() =>
        {
            if (_waiters.TryRemove(userId, out var pending))
            {
                pending.TrySetCanceled(cancellationToken);
            }
        });

        try
        {
            return await tcs.Task;
        }
        finally
        {
            _waiters.TryRemove(userId, out _);
        }
    }

    public MatchStatusResponse BuildStatusResponse(MatchQueueEntry entry)
    {
        var matchResult = GetMatchResultByUserId(entry.UserId);
        return new MatchStatusResponse
        {
            UserId = entry.UserId,
            Username = entry.Username,
            Status = entry.Status.ToString(),
            JoinedAtUtc = entry.JoinedAtUtc,
            MatchId = entry.MatchId,
            ServerAddress = matchResult?.ServerAddress
        };
    }

    public Result<MatchQueueEntry> Join(string userId, string username)
    {
        lock (_lock)
        {
            // client exist in entries
            if (_entries.TryGetValue(userId, out var existing))
            {
                if (existing.Status == MatchStatus.Waiting)
                    return Result<MatchQueueEntry>.Failure("ALREADY_IN_QUEUE", "이미 매칭 큐에 참가 중입니다.");

                if (existing.Status == MatchStatus.Matched)
                    return Result<MatchQueueEntry>.Failure("ALREADY_MATCHED", "이미 매칭이 완료되었습니다.");

                existing.Status = MatchStatus.Waiting;
                existing.JoinedAtUtc = DateTime.UtcNow;
                existing.MatchId = null;
                existing.Username = username;

                _waitingQueue.Enqueue(userId);
                return Result<MatchQueueEntry>.Success(existing);
            }

            // new client request
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

    public List<MatchQueueEntry>? TryMakeMatches()
    {
        lock (_lock)
        {
            var candidates = new List<MatchQueueEntry>();
            while (_waitingQueue.Count > 0 && candidates.Count < _globalFields.PlayerCount)
            {
                var userId = _waitingQueue.Dequeue();

                if (!_entries.TryGetValue(userId, out var entry))
                    continue;

                if (entry.Status != MatchStatus.Waiting)
                    continue;

                candidates.Add(entry);
            }

            if (candidates.Count < _globalFields.PlayerCount)
            {
                foreach (var candidate in candidates)
                {
                    _waitingQueue.Enqueue(candidate.UserId);
                }

                return null;
            }

            var matchId = Guid.NewGuid().ToString("N");
            var result = new MatchResult
            {
                MatchId = matchId,
                MatchedAtUtc = DateTime.UtcNow,
                UserIds = candidates.Select(x => x.UserId).ToList(),
                ServerAddress = "localhost:54800"
            };

            _matchResults[matchId] = result;

            foreach (var entry in candidates)
            {
                entry.Status = MatchStatus.Matched;
                entry.MatchId = matchId;

                if (_waiters.TryRemove(entry.UserId, out var waiter))
                {
                    waiter.TrySetResult(BuildStatusResponse(entry));
                }
            }

            return candidates;
        }
    }
}