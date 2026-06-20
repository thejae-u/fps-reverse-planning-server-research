using AuthServer.Hubs;
using AuthServer.Models;
using AuthServer.Protos;
using AuthServer.Services.Tcp;
using Microsoft.AspNetCore.SignalR;
using Microsoft.Extensions.Hosting;

namespace AuthServer.Services;

public class MatchWorker : BackgroundService
{
    private readonly MatchService _matchService;
    private readonly LogicServerConnectionPool _tcpPool;
    private readonly IHubContext<MatchHub> _hubContext;
    private readonly ILogger<MatchWorker> _logger;

    public MatchWorker(MatchService matchService, LogicServerConnectionPool tcpPool, IHubContext<MatchHub> hubContext, ILogger<MatchWorker> logger)
    {
        _matchService = matchService;
        _tcpPool = tcpPool;
        _hubContext = hubContext;
        _logger = logger;
    }

    protected override async Task ExecuteAsync(CancellationToken stoppingToken)
    {
        _logger.LogInformation("MatchWorker started");

        while (!stoppingToken.IsCancellationRequested)
        {
            try
            {
                var matchResult = await _matchService.TryMakeMatchesAsync();
                if (matchResult is not null)
                    await HandleMatchCreation(matchResult, stoppingToken);

                await Task.Delay(100, stoppingToken);

            }
            catch (OperationCanceledException)
            {
                break;
            }
            catch (Exception ex)
            {
                _logger.LogError(ex, "Error occured in MatchWorker");
                await Task.Delay(100, stoppingToken);
            }
        }

        _logger.LogInformation("MatchWorkerStopped");
    }

    private async Task HandleMatchCreation(MatchResult result, CancellationToken ct)
    {
        // Craete GUID per User
        var userTokenMap = result.UserIds.ToDictionary(uid => uid, _ => Guid.NewGuid().ToString("N"));

        var client = await _tcpPool.RentAsync();
        try
        {
            var request = new GamePacket
            {
                MatchCreateReq = new MatchCreateRequest
                {
                    MatchId = result.MatchId,
                    GameType = "Normal"
                }
            };

            // Include User Id and Token
            foreach (var kvp in userTokenMap)
            {
                request.MatchCreateReq.Users.Add(new UserSessionInfo
                {
                    UserId = kvp.Key,
                    SessionToken = kvp.Value
                });
            }

            // Send to Logic Server
            var responsePacket = await client.SendRequestAsync(request, ct);
            if (responsePacket.PayloadCase == GamePacket.PayloadOneofCase.MatchCreateRes)
            {
                var res = responsePacket.MatchCreateRes;
                foreach (var kvp in userTokenMap)
                {
                    var userId = kvp.Key;
                    var token = kvp.Value;

                    // SignalR send per user with token
                    await _hubContext.Clients.Group(MatchHub.GetUserGroup(userId)).SendAsync("Matched", new
                    {
                        matchId = result.MatchId,
                        serverAddress = $"{res.ServerIp}:{res.Port}",
                        sessionToken = token,
                        matchedAtUtc = DateTime.UtcNow
                    }, ct);
                }
            }
        }
        finally
        {
            _tcpPool.Return(client);
        }
    }
}