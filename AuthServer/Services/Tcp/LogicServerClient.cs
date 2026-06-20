using System.Collections.Concurrent;
using System.Net.Sockets;
using AuthServer.Protos;
using Serilog;

namespace AuthServer.Services.Tcp;

public class LogicServerClient : IDisposable
{
    private TcpClient? _client;
    private NetworkStream? _stream;
    private uint _nextSequenceId = 0;
    private bool _isDisposed = false;

    private readonly ConcurrentDictionary<uint, TaskCompletionSource<GamePacket>> _pendingRequests = new();
    private readonly CancellationTokenSource _cts = new();

    public event Action<GamePacket>? OnNotificationReceived;

    public LogicServerClient() { }

    public LogicServerClient(TcpClient existingClient)
    {
        _client = existingClient;
        _stream = _client.GetStream();
        StartReceiveLoop();
    }

    public async Task ConnectAsync(string host, int port)
    {
        _client = new TcpClient();
        await _client.ConnectAsync(host, port);
        
        Log.Information("Client connected");
        
        _stream = _client.GetStream();
        StartReceiveLoop();
    }

    private void StartReceiveLoop()
    {
        _ = Task.Run(() => ReceiveLoopAsync(_cts.Token));
    }

    public async Task<GamePacket> SendRequestAsync(GamePacket packet, CancellationToken userToken)
    {
        if (_stream == null || _isDisposed) throw new ObjectDisposedException(nameof(LogicServerClient));

        uint seqId = Interlocked.Increment(ref _nextSequenceId);
        packet.SequenceId = seqId;

        var tcs = new TaskCompletionSource<GamePacket>(TaskCreationOptions.RunContinuationsAsynchronously);
        _pendingRequests[seqId] = tcs;

        using var linkedCts = CancellationTokenSource.CreateLinkedTokenSource(userToken, _cts.Token);

        try
        {
            byte[] data = PacketSerializer.Serialize(packet);
            await _stream.WriteAsync(data, linkedCts.Token);
            await _stream.FlushAsync(linkedCts.Token);

            return await tcs.Task.WaitAsync(linkedCts.Token);
        }
        catch (Exception)
        {
            _pendingRequests.TryRemove(seqId, out _);
            throw;
        }
    }

    private async Task ReceiveLoopAsync(CancellationToken token)
    {
        byte[] headerBuffer = new byte[PacketSerializer.HeaderSize];

        try
        {
            while (!token.IsCancellationRequested && _client is { Connected: true })
            {
                //int read = await ReadExactlyAsync(headerBuffer, PacketSerializer.HeaderSize, token); // old-version
                //if (read == 0) break;
                await _stream!.ReadExactlyAsync(headerBuffer, 0, PacketSerializer.HeaderSize, token);

                // Serializer를 통해 헤더 파싱 (엔디안 변환 포함)
                ushort bodySize = PacketSerializer.DeserializeHeader(headerBuffer);

                byte[] bodyBuffer = new byte[bodySize];
                //await ReadExactlyAsync(bodyBuffer, bodySize, token); // old-version
                await _stream!.ReadExactlyAsync(bodyBuffer, 0, bodySize, token);

                var packet = PacketSerializer.Deserialize(bodyBuffer);
                Dispatch(packet);
            }
        }
        catch (OperationCanceledException)
        {
        }
        catch (EndOfStreamException)
        {
            Log.Information("Client disconnected");
        }
        catch (Exception ex)
        {
            Log.Error(ex, "[TCP Client Error] {Message}", ex.Message);
        }
        finally
        {
            Dispose();
        }
    }

    private void Dispatch(GamePacket packet)
    {
        if (packet.SequenceId > 0 && _pendingRequests.TryRemove(packet.SequenceId, out var tcs))
        {
            tcs.TrySetResult(packet);
        }
        else
        {
            OnNotificationReceived?.Invoke(packet);
        }
    }

    private async Task<int> ReadExactlyAsync(byte[] buffer, int size, CancellationToken token)
    {
        int totalRead = 0;
        while (totalRead < size)
        {
            int read = await _stream!.ReadAsync(buffer.AsMemory(totalRead, size - totalRead), token);
            if (read == 0) return 0;
            totalRead += read;
        }
        return totalRead;
    }

    public bool IsConnected => _client is { Connected: true } && !_isDisposed;

    public void Dispose()
    {
        if (_isDisposed) return;
        _isDisposed = true;

        _cts.Cancel();
        
        foreach (var tcs in _pendingRequests.Values)
        {
            tcs.TrySetException(new SocketException((int)SocketError.ConnectionAborted));
        }
        _pendingRequests.Clear();

        _stream?.Dispose();
        _client?.Dispose();
        _cts.Dispose();
    }
}
