using Microsoft.Extensions.Hosting;

namespace AuthServer.Services;

public class MatchWorker : BackgroundService
{
    private readonly MatchService _matchService;
    private readonly ILogger<MatchWorker> _logger;

    public MatchWorker(MatchService matchService, ILogger<MatchWorker> logger)
    {
        _matchService = matchService;
        _logger = logger;
    }

    protected override async Task ExecuteAsync(CancellationToken stoppingToken)
    {
        _logger.LogInformation("MatchWorker started");

        while (!stoppingToken.IsCancellationRequested)
        {
            try
            {
                await _matchService.TryMakeMatchesAsync();
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
}