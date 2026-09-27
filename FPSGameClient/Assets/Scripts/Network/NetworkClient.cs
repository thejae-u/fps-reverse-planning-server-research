using System;
using UnityEngine;
using System.Net;
using System.Net.Sockets;
using System.Threading;
using System.Threading.Tasks;
using Google.Protobuf;
using Protocol;

public class NetworkClient
{
    private TcpClient _client;
    private NetworkStream _tcpStream;
    private UdpClient _udpClient;
    private IPEndPoint _ep;
    private IPEndPoint _udpServerEp;
    private CancellationTokenSource _sessionCts;

    public bool IsConnected => _client != null && _client.Connected;
    public bool IsUdpAuthenticated { get; private set; }
    public string SessionId { get; private set; }
    public string RoomId { get; private set; }
    public int ServerUdpPort { get; private set; }
    public ulong LastServerTick { get; private set; }

    public event Action<NetworkPacket> OnTcpPacketReceived;
    public event Action<NetworkPacket> OnUdpPacketReceived;
    public event Action<string, int> OnHandshakeCompleted;
    public event Action<Matchmaking> OnMatchmakingReceived;
    public event Action<IngamePacket> OnIngamePacketReceived;

    public void Init(string ip = "", int port = 0)
    {
        if (string.IsNullOrEmpty(ip))
        {
            Debug.Assert(false, $"Invalid ip: {ip}");
            return;
        }

        if (port == 0)
        {
            Debug.Assert(false, $"Invalid port: {port}");
            return;
        }

        _ep = new IPEndPoint(IPAddress.Parse(ip), port);
        _client = new TcpClient(AddressFamily.InterNetwork);
        _udpClient = new UdpClient(0, AddressFamily.InterNetwork);
    }

    public async Task ConnectAsync(CancellationToken ct = default)
    {
        if (_ep == null)
        {
            Debug.LogError("[NetworkClient] EndPoint is not initialized. Call Init() first.");
            return;
        }

        if (_client == null || _client.Client == null)
        {
            _client = new TcpClient(AddressFamily.InterNetwork);
        }

        if (_udpClient == null || _udpClient.Client == null)
        {
            _udpClient = new UdpClient(0, AddressFamily.InterNetwork);
        }

        _sessionCts?.Cancel();
        _sessionCts?.Dispose();
        _sessionCts = CancellationTokenSource.CreateLinkedTokenSource(ct);
        CancellationToken sessionToken = _sessionCts.Token;

        try
        {
            sessionToken.ThrowIfCancellationRequested();

            await ConnectWithCancellationAsync(_client, _ep.Address, _ep.Port, sessionToken);
            _tcpStream = _client.GetStream();

            Debug.Log($"[NetworkClient] Connected to {_ep}");

            _ = TcpReceiveLoopAsync(sessionToken);
            _ = UdpReceiveLoopAsync(sessionToken);
        }
        catch (OperationCanceledException)
        {
            SafeCloseClient();
            Debug.LogWarning("Cancelled");
            return;
        }
        catch (Exception ex)
        {
            SafeCloseClient();
            Debug.LogError($"[NetworkClient] ConnectAsync failed: {ex.Message}");
            return;
        }
    }

    /// <summary>
    /// .NET Standard 2.1(Unity 6) 환경에서 CancellationToken 매개변수가 없는
    /// TcpClient.ConnectAsync를 안전하게 취소 및 소켓 정리하기 위한 래퍼 메서드
    /// </summary>
    private static async Task ConnectWithCancellationAsync(
        TcpClient client,
        IPAddress address,
        int port,
        CancellationToken ct)
    {
        var tcs = new TaskCompletionSource<bool>(TaskCreationOptions.RunContinuationsAsynchronously);

        using (ct.Register(state =>
        {
            var source = (TaskCompletionSource<bool>)state;
            source.TrySetCanceled(ct);
        }, tcs))
        {
            Task connectTask = client.ConnectAsync(address, port);

            // 취소로 인해 소켓을 닫을 때 백그라운드 connectTask에서 발생하는 예외가
            // UnobservedTaskException으로 전파되지 않도록 관찰 처리
            _ = connectTask.ContinueWith(
                t => _ = t.Exception,
                TaskContinuationOptions.OnlyOnFaulted | TaskContinuationOptions.ExecuteSynchronously);

            Task completedTask = await Task.WhenAny(connectTask, tcs.Task);

            if (completedTask == tcs.Task)
            {
                client.Close();
                await tcs.Task; // OperationCanceledException 발생
            }

            if (ct.IsCancellationRequested)
            {
                client.Close();
                ct.ThrowIfCancellationRequested();
            }

            await connectTask;
        }
    }

    #region Protobuf Framing & Send / Receive

    /// <summary>
    /// NetworkPacket을 2바이트 Big-Endian(Network Byte Order) 길이 헤더와 함께 직렬화합니다.
    /// </summary>
    public static byte[] SerializeWithHeader(NetworkPacket packet)
    {
        byte[] payload = packet.ToByteArray();
        short netLength = IPAddress.HostToNetworkOrder((short)payload.Length);
        byte[] headerBytes = BitConverter.GetBytes(netLength);

        byte[] frame = new byte[2 + payload.Length];
        Buffer.BlockCopy(headerBytes, 0, frame, 0, 2);
        Buffer.BlockCopy(payload, 0, frame, 2, payload.Length);
        return frame;
    }

    public static NetworkPacket CreateNetworkPacket(PacketType type, IMessage innerMessage = null)
    {
        return new NetworkPacket
        {
            Type = type,
            Data = innerMessage != null ? innerMessage.ToByteString() : ByteString.Empty
        };
    }

    public async Task SendTcpPacketAsync(PacketType type, IMessage innerMessage, CancellationToken ct = default)
    {
        NetworkPacket packet = CreateNetworkPacket(type, innerMessage);
        await SendTcpPacketAsync(packet, ct);
    }

    public async Task SendTcpPacketAsync(NetworkPacket packet, CancellationToken ct = default)
    {
        if (!IsConnected || _tcpStream == null) return;

        byte[] frame = SerializeWithHeader(packet);
        await _tcpStream.WriteAsync(frame, 0, frame.Length, ct);
    }

    public async Task SendUdpPacketAsync(PacketType type, IMessage innerMessage)
    {
        NetworkPacket packet = CreateNetworkPacket(type, innerMessage);
        await SendUdpPacketAsync(packet);
    }

    public async Task SendUdpPacketAsync(NetworkPacket packet)
    {
        if (_udpClient == null || _udpServerEp == null) return;

        byte[] frame = SerializeWithHeader(packet);
        await _udpClient.SendAsync(frame, frame.Length, _udpServerEp);
    }

    public async Task SendUdpHolePunchingAsync()
    {
        if (string.IsNullOrEmpty(SessionId) || ServerUdpPort <= 0) return;

        var authPacket = new AuthenticationPacket
        {
            Method = AuthenticationType.UdpHolePunching,
            SessionId = ByteString.CopyFromUtf8(SessionId),
            RoomId = string.IsNullOrEmpty(RoomId) ? ByteString.Empty : ByteString.CopyFromUtf8(RoomId)
        };

        await SendUdpPacketAsync(PacketType.Authentication, authPacket);
    }

    public async Task SendIngamePacketAsync(IngameType method, IMessage innerData, ulong clientTick = 0)
    {
        ByteString payload = innerData != null ? innerData.ToByteString() : ByteString.Empty;
        await SendIngameRawBytesAsync(method, payload, clientTick);
    }

    public async Task SendIngameRawBytesAsync(IngameType method, ByteString rawData, ulong clientTick = 0)
    {
        if (string.IsNullOrEmpty(SessionId)) return;

        var ingamePacket = new IngamePacket
        {
            SessionId = ByteString.CopyFromUtf8(SessionId),
            RoomId = string.IsNullOrEmpty(RoomId) ? ByteString.Empty : ByteString.CopyFromUtf8(RoomId),
            Method = method,
            Data = rawData ?? ByteString.Empty,
            ClientTick = clientTick > 0 ? clientTick : LastServerTick
        };

        await SendUdpPacketAsync(PacketType.Ingame, ingamePacket);
    }

    public async Task SendMoveAsync(Vector3 origin, Vector3 direction)
    {
        if (string.IsNullOrEmpty(SessionId) || !IsUdpAuthenticated) return;

        var movePacket = new MovePacket
        {
            PlayerId = ByteString.CopyFromUtf8(SessionId),
            OriginX = origin.x,
            OriginY = origin.y,
            OriginZ = origin.z,
            DirX = direction.x,
            DirY = direction.y,
            DirZ = direction.z
        };

        await SendIngamePacketAsync(IngameType.Move, movePacket, LastServerTick);
    }

    public async Task SendJumpAsync()
    {
        if (string.IsNullOrEmpty(SessionId) || !IsUdpAuthenticated) return;
        await SendIngameRawBytesAsync(IngameType.Jump, ByteString.Empty, LastServerTick);
    }

    /// <summary>
    /// C++ 서버 World::ProcessQueue의 IngameType::Shoot 규격(12바이트 Vector3 float[3] + clientTick)에 맞춰 전송합니다.
    /// </summary>
    public async Task SendShootAsync(Vector3 direction)
    {
        if (string.IsNullOrEmpty(SessionId) || !IsUdpAuthenticated) return;

        Vector3 normDir = direction.normalized;
        byte[] dirBytes = new byte[12];
        Buffer.BlockCopy(BitConverter.GetBytes(normDir.x), 0, dirBytes, 0, 4);
        Buffer.BlockCopy(BitConverter.GetBytes(normDir.y), 0, dirBytes, 4, 4);
        Buffer.BlockCopy(BitConverter.GetBytes(normDir.z), 0, dirBytes, 8, 4);

        await SendIngameRawBytesAsync(IngameType.Shoot, ByteString.CopyFrom(dirBytes), LastServerTick);
    }

    private async Task TcpReceiveLoopAsync(CancellationToken ct)
    {
        var headerBuffer = new byte[2];

        try
        {
            while (!ct.IsCancellationRequested && IsConnected)
            {
                bool headerRead = await ReadExactAsync(_tcpStream, headerBuffer, 2, ct);
                if (!headerRead) break;

                short netBodySize = BitConverter.ToInt16(headerBuffer, 0);
                ushort bodySize = (ushort)IPAddress.NetworkToHostOrder(netBodySize);
                byte[] bodyBuffer = new byte[bodySize];

                bool bodyRead = await ReadExactAsync(_tcpStream, bodyBuffer, bodySize, ct);
                if (!bodyRead) break;

                NetworkPacket packet = NetworkPacket.Parser.ParseFrom(bodyBuffer, 0, bodySize);
                HandleTcpPacket(packet);
            }
        }
        catch (OperationCanceledException)
        {
            // Normal shutdown via CancellationToken
        }
        catch (Exception ex)
        {
            if (!ct.IsCancellationRequested)
            {
                Debug.LogWarning($"[NetworkClient] TCP Receive Loop ended: {ex.Message}");
            }
        }
    }

    private async Task UdpReceiveLoopAsync(CancellationToken ct)
    {
        try
        {
            while (!ct.IsCancellationRequested && _udpClient != null)
            {
                UdpReceiveResult result = await _udpClient.ReceiveAsync();
                byte[] buffer = result.Buffer;
                if (buffer == null || buffer.Length < 2) continue;

                short netExpectedSize = BitConverter.ToInt16(buffer, 0);
                ushort expectedSize = (ushort)IPAddress.NetworkToHostOrder(netExpectedSize);
                int payloadSize = buffer.Length - 2;
                if (expectedSize != payloadSize) continue;

                NetworkPacket packet = NetworkPacket.Parser.ParseFrom(buffer, 2, payloadSize);
                HandleUdpPacket(packet);
            }
        }
        catch (ObjectDisposedException)
        {
            // UdpClient closed on Disconnect
        }
        catch (OperationCanceledException)
        {
            // Normal shutdown
        }
        catch (Exception ex)
        {
            if (!ct.IsCancellationRequested)
            {
                Debug.LogWarning($"[NetworkClient] UDP Receive Loop ended: {ex.Message}");
            }
        }
    }

    private void HandleTcpPacket(NetworkPacket packet)
    {
        switch (packet.Type)
        {
            case PacketType.PortHandshake:
            {
                string payload = packet.Data.ToStringUtf8();
                int commaIdx = payload.IndexOf(',');
                if (commaIdx > 0 && int.TryParse(payload.Substring(0, commaIdx), out int udpPort))
                {
                    ServerUdpPort = udpPort;
                    SessionId = payload.Substring(commaIdx + 1);
                }
                else
                {
                    PortHandshakePacket hs = PortHandshakePacket.Parser.ParseFrom(packet.Data);
                    ServerUdpPort = (int)hs.ServerPort;
                    SessionId = hs.SessionId.ToStringUtf8();
                }

                _udpServerEp = new IPEndPoint(_ep.Address, ServerUdpPort);
                Debug.Log($"[NetworkClient] Handshake OK - SessionId: {SessionId}, UDP Port: {ServerUdpPort}");
                OnHandshakeCompleted?.Invoke(SessionId, ServerUdpPort);
                _ = SendUdpHolePunchingAsync();
                break;
            }
            case PacketType.Match:
            {
                Matchmaking match = Matchmaking.Parser.ParseFrom(packet.Data);
                if (match.HasRoomId)
                {
                    RoomId = match.RoomId.ToStringUtf8();
                }
                OnMatchmakingReceived?.Invoke(match);
                break;
            }
            case PacketType.Ingame:
            {
                // ScoreboardPacket 등은 서버에서 TCP로도 전송됨
                IngamePacket ingame = IngamePacket.Parser.ParseFrom(packet.Data);
                if (ingame.ClientTick > 0) LastServerTick = ingame.ClientTick;
                OnIngamePacketReceived?.Invoke(ingame);
                break;
            }
        }

        OnTcpPacketReceived?.Invoke(packet);
    }

    private void HandleUdpPacket(NetworkPacket packet)
    {
        switch (packet.Type)
        {
            case PacketType.Authentication:
            {
                AuthenticationPacket auth = AuthenticationPacket.Parser.ParseFrom(packet.Data);
                if (auth.Method == AuthenticationType.AuthenticationOk)
                {
                    IsUdpAuthenticated = true;
                    Debug.Log($"[NetworkClient] UDP Hole Punching Authenticated! (SessionId: {SessionId})");
                }
                break;
            }
            case PacketType.Ingame:
            {
                IngamePacket ingame = IngamePacket.Parser.ParseFrom(packet.Data);
                if (ingame.ClientTick > 0) LastServerTick = ingame.ClientTick;
                if (string.IsNullOrEmpty(RoomId) && !ingame.RoomId.IsEmpty)
                {
                    RoomId = ingame.RoomId.ToStringUtf8();
                }
                OnIngamePacketReceived?.Invoke(ingame);
                break;
            }
        }

        OnUdpPacketReceived?.Invoke(packet);
    }

    private static async Task<bool> ReadExactAsync(NetworkStream stream, byte[] buffer, int count, CancellationToken ct)
    {
        int offset = 0;
        while (offset < count)
        {
            int bytesRead = await stream.ReadAsync(buffer, offset, count - offset, ct);
            if (bytesRead == 0) return false;
            offset += bytesRead;
        }
        return true;
    }

    #endregion

    public void Disconnect()
    {
        _sessionCts?.Cancel();
        _sessionCts?.Dispose();
        _sessionCts = null;

        SafeCloseClient();
    }

    private void SafeCloseClient()
    {
        try
        {
            _tcpStream?.Close();
            _tcpStream?.Dispose();
        }
        catch (Exception) { }
        finally
        {
            _tcpStream = null;
        }

        if (_client != null)
        {
            try
            {
                _client.Close();
                _client.Dispose();
            }
            catch (Exception) { }
            finally
            {
                _client = null;
            }
        }

        if (_udpClient != null)
        {
            try
            {
                _udpClient.Close();
                _udpClient.Dispose();
            }
            catch (Exception) { }
            finally
            {
                _udpClient = null;
            }
        }
    }
}