using System.Collections.Concurrent;
using Microsoft.Extensions.Options;
using AuthServer.Protos;

namespace AuthServer.Services.Tcp;

public class LogicServerConnectionPool : IDisposable
{
    private readonly ConcurrentBag<LogicServerClient> _pool = new();
    private readonly TcpOptions _options;
    private readonly ILogger<LogicServerConnectionPool> _logger;

    public event Action<GamePacket>? OnGlobalNotificationReceived;

    public LogicServerConnectionPool(IOptions<TcpOptions> options, ILogger<LogicServerConnectionPool> logger)
    {
        _options = options.Value;
        _logger = logger;
    }

    public void RegisterExternalConnection(LogicServerClient client)
    {
        client.OnNotificationReceived += (packet) => OnGlobalNotificationReceived?.Invoke(packet);
        _pool.Add(client);
        _logger.LogInformation("New external connection registered to pool. Total connections in pool: {Count}", _pool.Count);
    }

    public async Task<LogicServerClient> RentAsync()
    {
        while (_pool.TryTake(out var client))
        {
            if (client.IsConnected) return client;
            client.Dispose();
        }

        _logger.LogInformation("Creating new multiplexed TCP connection to {Host}:{Port}", _options.Host, _options.ReqPort);
        
        var newClient = new LogicServerClient();
        newClient.OnNotificationReceived += (packet) => OnGlobalNotificationReceived?.Invoke(packet);

        await newClient.ConnectAsync(_options.Host, _options.ReqPort);
        return newClient;
    }

    public void Return(LogicServerClient client)
    {
        if (client.IsConnected)
        {
            _pool.Add(client);
        }
        else
        {
            client.Dispose();
        }
    }

    public void Dispose()
    {
        while (_pool.TryTake(out var client))
        {
            client.Dispose();
        }
    }
}
