using System.Net;
using System.Net.Sockets;
using Microsoft.Extensions.Options;

namespace AuthServer.Services.Tcp;

public class LogicServerListenerService : BackgroundService
{
    private readonly LogicServerConnectionPool _pool;
    private readonly TcpOptions _options;
    private readonly ILogger<LogicServerListenerService> _logger;

    public LogicServerListenerService(
        LogicServerConnectionPool pool, 
        IOptions<TcpOptions> options, 
        ILogger<LogicServerListenerService> logger)
    {
        _pool = pool;
        _options = options.Value;
        _logger = logger;
    }

    protected override async Task ExecuteAsync(CancellationToken stoppingToken)
    {
        var listener = new TcpListener(IPAddress.Any, _options.Port);
        listener.Start();
        
        _logger.LogInformation("LogicServerListenerService started on port {Port}", _options.Port);

        while (!stoppingToken.IsCancellationRequested)
        {
            try
            {
                var tcpClient = await listener.AcceptTcpClientAsync(stoppingToken);
                _logger.LogInformation("Inbound Logic Server connection accepted from {Remote}", tcpClient.Client.RemoteEndPoint);

                var client = new LogicServerClient(tcpClient);
                _pool.RegisterExternalConnection(client);
            }
            catch (OperationCanceledException) { break; }
            catch (Exception ex)
            {
                _logger.LogError(ex, "Error accepting inbound Logic Server connection");
            }
        }

        listener.Stop();
    }
}
