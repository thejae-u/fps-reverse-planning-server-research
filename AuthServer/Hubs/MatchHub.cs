using System.Diagnostics;
using System.Security.Claims;
using System.Text.RegularExpressions;
using Microsoft.AspNetCore.Authorization;
using Microsoft.AspNetCore.SignalR;

namespace AuthServer.Hubs;

[Authorize]
public class MatchHub : Hub
{
    private readonly ILogger<MatchHub> _logger;

    public MatchHub(ILogger<MatchHub> logger)
    {
        _logger = logger;
    }

    public override async Task OnConnectedAsync()
    {
        var userId = Context.User?.FindFirstValue(ClaimTypes.NameIdentifier);

        if (!string.IsNullOrEmpty(userId))
            await Groups.AddToGroupAsync(Context.ConnectionId, GetUserGroup(userId));

        await base.OnConnectedAsync();
    }

    public override async Task OnDisconnectedAsync(Exception? exception)
    {
        var userId = Context.User?.FindFirstValue(ClaimTypes.NameIdentifier);

        if (!string.IsNullOrEmpty(userId))
            await Groups.RemoveFromGroupAsync(Context.ConnectionId, GetUserGroup(userId));

        await base.OnDisconnectedAsync(exception);
    }

    public Task Ping()
    {
        return Clients.Caller.SendAsync("Pong", new
        {
            Message = "connected",
            Time = DateTime.UtcNow
        });
    }

    public async Task JoinMatchChat(string matchId)
    {
        if (string.IsNullOrWhiteSpace(matchId))
            throw new HubException("matchId is required.");

        var userId = Context.User?.FindFirstValue(ClaimTypes.NameIdentifier);
        var nickname = GetNickName();

        if (string.IsNullOrWhiteSpace(userId))
            throw new HubException("Unauthorized.");

        var matchGroup = GetMatchGroup(matchId);
        await Groups.AddToGroupAsync(Context.ConnectionId, matchGroup);

        await Clients.Caller.SendAsync("JoinedMatchChat", new
        {
            matchId,
            connectionId = Context.ConnectionId
        });

        await Clients.Group(matchGroup).SendAsync("SystemMessage", new
        {
            matchId,
            message = $"{nickname} 님이 입장했습니다.",
            sendAt = DateTimeOffset.UtcNow
        });
    }

    public async Task LeaveMatchChat(string matchId)
    {
        if (string.IsNullOrWhiteSpace(matchId))
            throw new HubException("matchId is required.");

        var nickname = GetNickName();
        var matchGroup = GetMatchGroup(matchId);

        await Groups.RemoveFromGroupAsync(Context.ConnectionId, matchGroup);

        await Clients.Group(matchGroup).SendAsync("SystemMessage", new
        {
            matchId,
            message = $"{nickname} 님이 채팅방에서 나갔습니다.",
            sendAt = DateTimeOffset.UtcNow
        });

        await Clients.Caller.SendAsync("LeftMatchChat", new
        {
            matchId,
            connectionId = Context.ConnectionId
        });
    }

    public async Task SendMatchMessage(string matchId, string message)
    {
        if (string.IsNullOrWhiteSpace(matchId))
            throw new HubException("matchId is required.");

        if (string.IsNullOrWhiteSpace(message))
            throw new HubException("message is required.");

        message = message.Trim();

        if (message.Length > 200)
            throw new HubException("messag is too long.");

        var userId = Context.User?.FindFirstValue(ClaimTypes.NameIdentifier);
        var nickname = GetNickName();

        if (string.IsNullOrWhiteSpace(userId))
            throw new HubException("Unauthorized.");

        var matchGroup = GetMatchGroup(matchId);

        await Clients.Group(matchGroup).SendAsync("ReceiveMatchMessage", new
        {
            matchId,
            sendeerUserId = userId,
            senderName = nickname,
            message,
            sendAt = DateTimeOffset.UtcNow
        });
    }

    public string GetNickName()
    {
        return Context.User?.Identity?.Name
            ?? Context.User?.FindFirstValue(ClaimTypes.Name)
            ?? Context.User?.FindFirstValue("nickname")
            ?? Context.User?.FindFirstValue("name")
            ?? "Unknown";
    }

    public static string GetUserGroup(string userId) => $"user:{userId}";
    public static string GetMatchGroup(string matchId) => $"match:{matchId}";

}