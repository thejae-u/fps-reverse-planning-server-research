using System.Diagnostics;
using System.Security.Claims;
using System.Text.RegularExpressions;
using AuthServer.Services;
using Microsoft.AspNetCore.Authorization;
using Microsoft.AspNetCore.SignalR;

namespace AuthServer.Hubs;

[Authorize]
public class MatchHub : Hub
{
    private readonly ILogger<MatchHub> _logger;
    private readonly MatchService _matchService;

    public MatchHub(ILogger<MatchHub> logger, MatchService matchService)
    {
        _logger = logger;
        _matchService = matchService;
    }

    public override async Task OnConnectedAsync()
    {
        var userId = Context.User?.FindFirstValue(ClaimTypes.NameIdentifier);

        if (!string.IsNullOrEmpty(userId))
        {
            await Groups.AddToGroupAsync(Context.ConnectionId, GetUserGroup(userId));
            _matchService.AddConnectionId(userId, Context.ConnectionId);
        }

        await base.OnConnectedAsync();
    }

    public override async Task OnDisconnectedAsync(Exception? exception)
    {
        var userId = Context.User?.FindFirstValue(ClaimTypes.NameIdentifier);

        if (!string.IsNullOrEmpty(userId))
        {
            await Groups.RemoveFromGroupAsync(Context.ConnectionId, GetUserGroup(userId));
            _matchService.RemoveConnectionId(userId);
        }

        await base.OnDisconnectedAsync(exception);
    }

    public static string GetUserGroup(string userId) => $"user:{userId}";
    public static string GetMatchGroup(string matchId) => $"match:{matchId}";

    public Task Ping()
    {
        return Clients.Caller.SendAsync("Pong", new
        {
            Message = "connected",
            Time = DateTime.UtcNow
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
        // match Id Check
        if (string.IsNullOrWhiteSpace(matchId))
            throw new HubException("matchId is required.");

        // message Check
        if (string.IsNullOrWhiteSpace(message))
            throw new HubException("message is required.");

        // message Length Check
        message = message.Trim();
        if (message.Length > 200)
            throw new HubException("messag is too long.");

        // user Id Get
        var userId = Context.User?.FindFirstValue(ClaimTypes.NameIdentifier);
        var nickname = GetNickName();

        // user id Check
        if (string.IsNullOrWhiteSpace(userId))
            throw new HubException("Unauthorized.");

        // invalid match check
        var userMatch = _matchService.GetMatchResultByUserId(userId);
        if(userMatch is null || string.IsNullOrEmpty(userMatch.MatchId) || userMatch.MatchId != matchId)
            throw new HubException("Invalid user match.");

        // message send
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
}