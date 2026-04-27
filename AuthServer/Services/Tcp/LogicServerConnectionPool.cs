using Microsoft.Extensions.Options;
using System.Collections.Concurrent;

namespace AuthServer.Services.Tcp;

public class LogicServerConnectionPool
{
    private readonly ConcurrentBag<LogicServerClient> _pool = new();
    private readonly TcpOptions _options;
    private ILogger<LogicServerConnectionPool> _logger;

    public LogicServerConnectionPool(IOptions<TcpOptions> options, ILogger<LogicServerConnectionPool> logger)
    {
        _options = options.Value;
        _logger = logger;
    }

    public async Task<LogicServerClient> RentAsync()
    {
        if(_pool.TryTake(out var client))
        {
            if (client.IsConnected) return client;
            client.Dispose();
        }

        _logger.LogInformation("Creating new TCP connection to Logic Server at {Host}:{Port}", _options.Host, _options.Port);
        var newClient = new LogicServerClient(_options.Host, _options.Port);
        await newClient.ConnectAsync();
        return newClient;
    }

    public void Return(LogicServerClient client)
    {
        if (client.IsConnected)
            _pool.Add(client);
        else
            client.Dispose();
    }
}
