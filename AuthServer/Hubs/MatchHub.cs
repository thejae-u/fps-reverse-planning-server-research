using System.Security.Claims;
using Microsoft.AspNetCore.Authorization;
using Microsoft.AspNetCore.Components;
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

    public static string GetUserGroup(string userId) => $"user:{userId}";
}