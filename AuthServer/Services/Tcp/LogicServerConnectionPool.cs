using System.Collections.Concurrent;
using System.Net.Sockets;
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

    public async Task<bool> InternalTestAsync()
    {
        const int maxRetries = 10;
        const int delayMs = 3000;

        for (int attempt = 1; attempt <= maxRetries; attempt++)
        {
            try
            {
                using var client = new TcpClient();
                using var cts = new CancellationTokenSource(TimeSpan.FromSeconds(3));
                _logger.LogInformation("[InternalTest] (Attempt {Attempt}/{MaxRetries}) Attempting connection to C++ Logic Server at {Host}:{Port}...", attempt, maxRetries, _options.Host, _options.ReqPort);
                await client.ConnectAsync(_options.Host, _options.ReqPort, cts.Token);
                _logger.LogInformation("[InternalTest] Successfully connected to C++ Logic Server at {Host}:{Port}", _options.Host, _options.ReqPort);
                return true;
            }
            catch (Exception ex)
            {
                _logger.LogWarning("[InternalTest] (Attempt {Attempt}/{MaxRetries}) Failed to connect to C++ Logic Server. Message: {Message}", attempt, maxRetries, ex.Message);
                if (attempt < maxRetries)
                {
                    await Task.Delay(delayMs);
                }
            }
        }

        _logger.LogError("[InternalTest] Connection validation failed after {MaxRetries} attempts.", maxRetries);
        return false;
    }

    public void Dispose()
    {
        while (_pool.TryTake(out var client))
        {
            client.Dispose();
        }
    }
}
