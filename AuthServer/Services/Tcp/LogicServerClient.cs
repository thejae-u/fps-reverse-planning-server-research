using Google.Protobuf;
using Microsoft.EntityFrameworkCore.Metadata.Conventions;
using System.Data;
using System.Net.Sockets;

namespace AuthServer.Services.Tcp;

public class LogicServerClient : IDisposable
{
    private TcpClient? _client;
    private NetworkStream? _stream;
    private readonly string _host;
    private readonly int _port;

    public LogicServerClient(string host, int port)
    {
        _host = host;
        _port = port;
    }

    public async Task ConnectAsync()
    {
        if (_client is { Connected: true }) return;

        _client = new TcpClient();
        await _client.ConnectAsync(_host, _port);
        _stream = _client.GetStream();
    }

    public async Task SendAsync<T>(T message) where T : IMessage
    {
        if (_stream is null) throw new InvalidOperationException("Not connected");

        byte[] data = PacketSerializer.Serialize(message);
        await _stream.WriteAsync(data, 0, data.Length);
        await _stream.FlushAsync();
    }

    public async Task<T> ReceiveAsync<T>(MessageParser<T> parser) where T : IMessage<T>
    {
        if (_stream is null) throw new InvalidOperationException("Not connected");

        byte[] header = new byte[PacketSerializer.HeaderSize];
        await ReadExactlyAsync(header, PacketSerializer.HeaderSize);

        if (!BitConverter.IsLittleEndian) Array.Reverse(header);
        ushort payloadSize = BitConverter.ToUInt16(header, 0);

        byte[] payload = new byte[payloadSize];
        await ReadExactlyAsync(payload, payloadSize);

        return parser.ParseFrom(payload);
    }

    public async Task ReadExactlyAsync(byte[] buffer, int size)
    {
        if (_stream is null) throw new InvalidOperationException("stream is null");

        int totalRead = 0;
        while (totalRead < size)
        {
            int read = await _stream.ReadAsync(buffer, totalRead, size - totalRead);
            if (read == 0) throw new SocketException((int)SocketError.ConnectionAborted);
            totalRead += read;
        }
    }

    public bool IsConnected => _client is { Connected: true };

    public void Dispose()
    {
        _stream?.Dispose();
        _client?.Dispose();
    }
}
