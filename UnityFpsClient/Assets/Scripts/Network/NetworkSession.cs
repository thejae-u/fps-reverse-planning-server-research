using System;
using System.IO;
using System.Net;
using System.Net.Sockets;
using System.Threading.Tasks;
using UnityEngine;
using Google.Protobuf;
using Protocol;

namespace Network
{
    public class NetworkSession
    {
        public string ServerIp { get; private set; }
        public int ServerPort { get; private set; }
        public bool IsMain { get; private set; }

        private TcpClient tcpClient;
        private UdpClient udpClient;
        private IPEndPoint serverUdpEndPoint;

        private string sessionId = "";
        private string roomId = "";
        private int serverUdpPort = 0;
        private bool isConnected = false;

        public NetworkSession(string ip, int port, bool isMain)
        {
            ServerIp = ip;
            ServerPort = port;
            IsMain = isMain;
        }

        public async void Connect()
        {
            try
            {
                tcpClient = new TcpClient();
                await tcpClient.ConnectAsync(ServerIp, ServerPort);
                isConnected = true;
                
                if (IsMain)
                    Debug.Log("Main TCP Connected to server.");
                else
                    Debug.Log("Dummy Bot TCP Connected to server.");

                // Start TCP read loop
                _ = ReceiveTcpLoop();
            }
            catch (Exception e)
            {
                Debug.LogError($"TCP Connection failed: {e.Message}");
            }
        }

        private async Task ReceiveTcpLoop()
        {
            var stream = tcpClient.GetStream();
            byte[] headerBuffer = new byte[2];

            try
            {
                while (isConnected)
                {
                    // Read header (2 bytes size in big endian)
                    int bytesRead = await ReadExactAsync(stream, headerBuffer, 2);
                    if (bytesRead == 0) break; // connection closed

                    ushort packetSize = (ushort)IPAddress.NetworkToHostOrder((short)BitConverter.ToUInt16(headerBuffer, 0));

                    // Read body
                    byte[] bodyBuffer = new byte[packetSize];
                    bytesRead = await ReadExactAsync(stream, bodyBuffer, packetSize);
                    if (bytesRead == 0) break;

                    // Parse Packet
                    var packet = Packet.Parser.ParseFrom(bodyBuffer);
                    HandleTcpPacket(packet);
                }
            }
            catch (Exception e)
            {
                Debug.LogError($"TCP Read error: {e.Message}");
            }
            finally
            {
                Disconnect();
            }
        }

        private void HandleTcpPacket(Packet packet)
        {
            if (packet.Type == PacketType.PortHandshake)
            {
                string rawData = packet.Data.ToStringUtf8();
                string[] parts = rawData.Split(',');
                if (parts.Length == 2)
                {
                    serverUdpPort = int.Parse(parts[0]);
                    sessionId = parts[1];

                    // Setup UDP and perform hole punching
                    InitializeUdp();
                    _ = SendUdpHolePunching();

                    // Request matchmaking
                    SendMatchRequest();
                }
            }
            else if (packet.Type == PacketType.Match)
            {
                var match = Matchmaking.Parser.ParseFrom(packet.Data);
                if (match.Type == MatchmakingType.Matched)
                {
                    roomId = match.RoomId.ToStringUtf8();
                    if (IsMain)
                    {
                        Debug.Log($"[Main] Matched! Room ID: {roomId}");
                        // Notify PacketHandler of matchmaking success
                        PacketHandler.Instance.EnqueueAction(() => {
                            NetworkManager.Instance.OnMainSessionMatched(roomId);
                        });
                    }
                }
            }
        }

        private void InitializeUdp()
        {
            udpClient = new UdpClient(0); // bind to random local port
            serverUdpEndPoint = new IPEndPoint(IPAddress.Parse(ServerIp), serverUdpPort);

            // Start UDP read loop
            _ = ReceiveUdpLoop();
        }

        private async Task SendUdpHolePunching()
        {
            var auth = new AuthenticationPacket
            {
                RoomId = ByteString.CopyFromUtf8(roomId),
                SessionId = ByteString.CopyFromUtf8(sessionId),
                Method = AuthenticationType.UdpHolePunching
            };

            var packet = new Packet
            {
                Type = PacketType.Authentication,
                Data = auth.ToByteString()
            };

            byte[] serialized = packet.ToByteArray();
            byte[] payload = PrepareUdpPayload(serialized);

            for (int i = 0; i < 5; i++)
            {
                await udpClient.SendAsync(payload, payload.Length, serverUdpEndPoint);
                await Task.Delay(100);
            }
        }

        private void SendMatchRequest()
        {
            var match = new Matchmaking
            {
                Type = MatchmakingType.Request,
                SessionId = ByteString.CopyFromUtf8(sessionId)
            };

            var packet = new Packet
            {
                Type = PacketType.Match,
                Data = match.ToByteString()
            };

            SendTcp(packet);
        }

        private async Task ReceiveUdpLoop()
        {
            try
            {
                while (isConnected)
                {
                    var result = await udpClient.ReceiveAsync();
                    byte[] data = result.Buffer;

                    if (data.Length >= 2)
                    {
                        ushort size = (ushort)IPAddress.NetworkToHostOrder((short)BitConverter.ToUInt16(data, 0));
                        if (data.Length >= 2 + size)
                        {
                            byte[] protoBytes = new byte[size];
                            Array.Copy(data, 2, protoBytes, 0, size);

                            var packet = Packet.Parser.ParseFrom(protoBytes);
                            if (packet.Type == PacketType.Ingame)
                            {
                                // Only the Main session forwards ingame packets to PacketHandler
                                if (IsMain)
                                {
                                    var ingame = IngamePacket.Parser.ParseFrom(packet.Data);
                                    PacketHandler.Instance.HandleIngamePacket(ingame);
                                }
                            }
                        }
                    }
                }
            }
            catch (Exception e)
            {
                Debug.LogError($"UDP Receive error: {e.Message}");
            }
        }

        public async void SendIngameUdp(IngameType method, byte[] subPacketData)
        {
            if (udpClient == null || string.IsNullOrEmpty(roomId)) return;

            try
            {
                var ingame = new IngamePacket
                {
                    RoomId = ByteString.CopyFromUtf8(roomId),
                    SessionId = ByteString.CopyFromUtf8(sessionId),
                    Method = method,
                    Data = ByteString.CopyFrom(subPacketData)
                };

                var packet = new Packet
                {
                    Type = PacketType.Ingame,
                    Data = ingame.ToByteString()
                };

                byte[] serialized = packet.ToByteArray();
                byte[] payload = PrepareUdpPayload(serialized);

                await udpClient.SendAsync(payload, payload.Length, serverUdpEndPoint);
            }
            catch (Exception e)
            {
                Debug.LogError($"UDP Send error: {e.Message}");
            }
        }

        public void SendTcp(Packet packet)
        {
            if (tcpClient == null || !tcpClient.Connected) return;

            try
            {
                byte[] serialized = packet.ToByteArray();
                ushort size = (ushort)serialized.Length;
                byte[] sizeHeader = BitConverter.GetBytes((ushort)IPAddress.HostToNetworkOrder((short)size));

                var stream = tcpClient.GetStream();
                stream.Write(sizeHeader, 0, 2);
                stream.Write(serialized, 0, serialized.Length);
            }
            catch (Exception e)
            {
                Debug.LogError($"TCP Send error: {e.Message}");
            }
        }

        private byte[] PrepareUdpPayload(byte[] serializedProto)
        {
            ushort size = (ushort)serializedProto.Length;
            byte[] sizeHeader = BitConverter.GetBytes((ushort)IPAddress.HostToNetworkOrder((short)size));
            byte[] payload = new byte[2 + size];
            Array.Copy(sizeHeader, 0, payload, 0, 2);
            Array.Copy(serializedProto, 0, payload, 2, size);
            return payload;
        }

        private async Task<int> ReadExactAsync(NetworkStream stream, byte[] buffer, int count)
        {
            int offset = 0;
            while (offset < count)
            {
                int read = await stream.ReadAsync(buffer, offset, count - offset);
                if (read == 0) return 0; // EOF
                offset += read;
            }
            return offset;
        }

        public void Disconnect()
        {
            isConnected = false;
            tcpClient?.Close();
            udpClient?.Close();
        }
    }
}
