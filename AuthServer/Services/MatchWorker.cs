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
    //private readonly LogicServerConnectionPool _tcpPool;
    private readonly DedicatedServerSpawner _spawner;
    private readonly IHubContext<MatchHub> _hubContext;
    private readonly ILogger<MatchWorker> _logger;

    public MatchWorker(MatchService matchService, DedicatedServerSpawner spawner, IHubContext<MatchHub> hubContext, ILogger<MatchWorker> logger)
    {
        _matchService = matchService;
        //_tcpPool = tcpPool;
        _spawner =  spawner;
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
        // Create GUID per User
        var userTokenMap = result.UserIds.ToDictionary(uid => uid, _ => Guid.NewGuid().ToString("N"));
        
        var serverInfo = await _spawner.SpawnServerAsync(result.MatchId, userTokenMap.Values.ToList());
        if (serverInfo == null)
        {
            _logger.LogError($"Failed to spawn dedicated server for match {result.MatchId}");
            return;
        }

        string serverIp = "127.0.0.1"; // for-test
        foreach (var kvp in userTokenMap)
        {
            var userId = kvp.Key;
            var token = kvp.Value;

            await _hubContext.Clients.Group(MatchHub.GetUserGroup(userId)).SendAsync("Matched", new
            {
                matchId = result.MatchId,
                tcpPort = serverInfo.TcpPort,
                udpPort = serverInfo.UdpPort,
                serverAddreess = $"{serverIp}:{serverInfo.TcpPort}",
                sessionToken = token,
                matchedAtUtc = DateTime.UtcNow
            }, ct);
        }
    }
}