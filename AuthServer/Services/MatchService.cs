using AuthServer.Models;

namespace AuthServer.Services;

public class MatchService
{
    private readonly Dictionary<string, MatchQueueEntry> _entries = new();
    private readonly object _lock = new();

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

                existing.Status = MatchStatus.Waiting;
                existing.JoinedAtUtc = DateTime.UtcNow;
                existing.Username = username;

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
}