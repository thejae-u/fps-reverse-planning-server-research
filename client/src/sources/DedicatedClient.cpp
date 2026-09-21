#include "DedicatedClient.hpp"
#include <iostream>

DedicatedClient::DedicatedClient(std::shared_ptr<IOManager> ioManager)
    : _ioManager(ioManager),
      _strand(asio::make_strand(ioManager->GetIoContext())),
      _tcpSocket(std::make_shared<asio::ip::tcp::socket>(_strand)),
      _udpSocket(_strand),
      _randomInputTimer(std::make_shared<asio::steady_timer>(_strand))
{
    InitUdpSocket();
}

DedicatedClient::~DedicatedClient()
{
    Disconnect();
}

void DedicatedClient::InitUdpSocket()
{
    std::error_code ec;
    if (_udpSocket.is_open())
    {
        _udpSocket.close(ec);
    }
    _udpSocket.open(asio::ip::udp::v4(), ec);
    if (ec)
    {
        AddLog("[UDP] Failed to open socket: " + ec.message());
        return;
    }
    _udpSocket.set_option(asio::socket_base::reuse_address(true));
    _udpSocket.bind(asio::ip::udp::endpoint(asio::ip::udp::v4(), 0), ec);
    if (ec)
    {
        AddLog("[UDP] Failed to bind socket: " + ec.message());
    }
    else
    {
        _clientUdpPort = _udpSocket.local_endpoint().port();
        AddLog("[UDP] Socket bound on local port: " + std::to_string(_clientUdpPort));
    }
}

void DedicatedClient::AddLog(const std::string& msg)
{
    std::lock_guard<std::mutex> lock(_logMutex);
    _logMessages.push_back(msg);
    spdlog::info("{}", msg);
}

std::vector<std::string> DedicatedClient::ConsumeLogs()
{
    std::lock_guard<std::mutex> lock(_logMutex);
    std::vector<std::string> logs = std::move(_logMessages);
    _logMessages.clear();
    return logs;
}

void DedicatedClient::Connect(const std::string& host, uint16_t port)
{
    if (_connected)
        return;

    _serverHost = host;
    AddLog(std::format("[TCP] Connecting to {}:{}...", host, port));

    try
    {
        asio::ip::tcp::resolver resolver(_ioManager->GetIoContext());
        auto endpoints = resolver.resolve(host, std::to_string(port));
        auto self = shared_from_this();

        _tcpSocket->async_connect(endpoints->endpoint(), asio::bind_executor(_strand, [this, self](std::error_code ec) {
            if (!ec)
            {
                _connected = true;
                std::error_code rec;
                auto endpoint = _tcpSocket->remote_endpoint(rec);
                if (!rec)
                {
                    _serverAddress = endpoint.address();
                    AddLog(std::format("[TCP] Connected successfully to {}:{}", endpoint.address().to_string(), endpoint.port()));
                }
                AsyncHandshake();
            }
            else
            {
                AddLog("[TCP] Connection failed: " + ec.message());
            }
        }));

        AsyncReadUdp();
    }
    catch (const std::exception& e)
    {
        AddLog(std::string("[TCP] Connect exception: ") + e.what());
    }
}

void DedicatedClient::Disconnect()
{
    StopRandomInput();
    bool wasConnected = _connected.exchange(false);
    _handshaked = false;
    _isIngame = false;
    _sessionId.clear();
    _roomId.clear();
    _serverUdpPort = 0;

    {
        std::lock_guard<std::mutex> lock(_sendTcpQueueMutex);
        while (!_sendTcpQueue.empty())
            _sendTcpQueue.pop();
        _isWriting = false;
    }

    std::error_code ec;
    if (_tcpSocket->is_open())
        _tcpSocket->close(ec);
    if (_udpSocket.is_open())
        _udpSocket.close(ec);

    _tcpSocket = std::make_shared<asio::ip::tcp::socket>(_strand);
    InitUdpSocket();

    AddLog("[TCP] Disconnected from Dedicated Server");
    if (wasConnected && _onDisconnected)
    {
        _onDisconnected("Connection closed by server or client");
    }
}

void DedicatedClient::AsyncHandshake()
{
    auto sizeHeader = std::make_shared<std::vector<uint8_t>>(2);
    auto self = shared_from_this();

    asio::async_read(*_tcpSocket, asio::buffer(*sizeHeader), asio::bind_executor(_strand, [this, self, sizeHeader](std::error_code ec, std::size_t) {
        if (ec)
        {
            AddLog("[Handshake] Header read error: " + ec.message());
            Disconnect();
            return;
        }

        uint16_t bodySize = (sizeHeader->at(0) << 8) | sizeHeader->at(1);
        auto bodyBuffer = std::make_shared<std::vector<uint8_t>>(bodySize);

        asio::async_read(*_tcpSocket, asio::buffer(*bodyBuffer), asio::bind_executor(_strand, [this, self, bodyBuffer](std::error_code ec, std::size_t) {
            if (ec)
            {
                AddLog("[Handshake] Body read error: " + ec.message());
                Disconnect();
                return;
            }

            Protocol::NetworkPacket packet;
            if (packet.ParseFromArray(bodyBuffer->data(), static_cast<int>(bodyBuffer->size())))
            {
                if (packet.type() == Protocol::PacketType::PortHandshake)
                {
                    std::string payload = packet.data();
                    auto commaPos = payload.find(',');
                    if (commaPos != std::string::npos)
                    {
                        _serverUdpPort = static_cast<uint16_t>(std::stoi(payload.substr(0, commaPos)));
                        _sessionId = payload.substr(commaPos + 1);
                        _handshaked = true;

                        AddLog(std::format("[Handshake] OK! Assigned SessionID: {}, Server UDP Port: {}", _sessionId, _serverUdpPort));
                        SendUdpHolePunching();
                    }
                }
            }

            // Continue reading general TCP packets
            AsyncReadTcp();
        }));
    }));
}

void DedicatedClient::SendUdpHolePunching()
{
    if (_serverUdpPort == 0 || _sessionId.empty())
        return;

    AddLog("[UDP] Sending Hole Punching packet...");

    Protocol::AuthenticationPacket authPacket;
    authPacket.set_method(Protocol::AuthenticationType::UdpHolePunching);
    authPacket.set_sessionid(_sessionId);

    std::string authData;
    authPacket.SerializeToString(&authData);

    Protocol::NetworkPacket packet;
    packet.set_type(Protocol::PacketType::Authentication);
    packet.set_data(authData);

    std::string serializedPacket;
    packet.SerializeToString(&serializedPacket);

    uint16_t totalSize = static_cast<uint16_t>(serializedPacket.size());
    uint16_t netSize = htons(totalSize);

    auto sendBuffer = std::make_shared<std::vector<uint8_t>>(2 + totalSize);
    std::memcpy(sendBuffer->data(), &netSize, 2);
    std::memcpy(sendBuffer->data() + 2, serializedPacket.data(), totalSize);

    asio::ip::udp::endpoint serverEndpoint(_serverAddress, _serverUdpPort);
    auto self = shared_from_this();

    _udpSocket.async_send_to(asio::buffer(*sendBuffer), serverEndpoint, asio::bind_executor(_strand, [this, self, sendBuffer](std::error_code ec, std::size_t bytes) {
        if (ec)
        {
            AddLog("[UDP] Hole punching send failed: " + ec.message());
        }
        else
        {
            AddLog(std::format("[UDP] Sent hole punching packet ({} bytes)", bytes));
        }
    }));
}

void DedicatedClient::AsyncReadUdp()
{
    if (!_udpSocket.is_open())
        return;

    auto self = shared_from_this();

    _udpSocket.async_receive_from(
        asio::buffer(_udpReadBuffer), _udpSenderEndpoint,
        asio::bind_executor(_strand, [this, self](std::error_code ec, std::size_t bytesReceived) {
            if (!ec && bytesReceived >= 2)
            {
                uint16_t expectedSize = (_udpReadBuffer[0] << 8) | _udpReadBuffer[1];
                std::size_t payloadSize = bytesReceived - 2;

                if (expectedSize == payloadSize)
                {
                    Protocol::NetworkPacket packet;
                    if (packet.ParseFromArray(_udpReadBuffer.data() + 2, static_cast<int>(payloadSize)))
                    {
                        if (packet.type() == Protocol::PacketType::Authentication)
                        {
                            Protocol::AuthenticationPacket auth;
                            if (auth.ParseFromString(packet.data()))
                            {
                                if (auth.method() == Protocol::AuthenticationType::AuthenticationOk)
                                {
                                    _isIngame = true;
                                    AddLog("[UDP] Hole Punching Authenticated! Ingame Ready.");
                                    if (_onIngameReady)
                                    {
                                        _onIngameReady();
                                    }
                                }
                            }
                        }
                        else if (packet.type() == Protocol::PacketType::Ingame)
                        {
                            AddLog(std::format("[UDP] Received Ingame packet ({} bytes)", payloadSize));
                        }
                    }
                }
            }

            if (_connected || _udpSocket.is_open())
            {
                AsyncReadUdp();
            }
        }));
}

void DedicatedClient::AsyncReadTcp()
{
    auto sizeHeader = std::make_shared<std::vector<uint8_t>>(2);
    auto self = shared_from_this();

    asio::async_read(*_tcpSocket, asio::buffer(*sizeHeader), asio::bind_executor(_strand, [this, self, sizeHeader](std::error_code ec, std::size_t) {
        if (ec)
        {
            AddLog("[TCP] Read error: " + ec.message());
            Disconnect();
            return;
        }

        uint16_t bodySize = (sizeHeader->at(0) << 8) | sizeHeader->at(1);
        auto bodyBuffer = std::make_shared<std::vector<uint8_t>>(bodySize);

        asio::async_read(*_tcpSocket, asio::buffer(*bodyBuffer), asio::bind_executor(_strand, [this, self, bodyBuffer](std::error_code ec, std::size_t) {
            if (ec)
            {
                AddLog("[TCP] Body read error: " + ec.message());
                Disconnect();
                return;
            }

            Protocol::NetworkPacket packet;
            if (packet.ParseFromArray(bodyBuffer->data(), static_cast<int>(bodyBuffer->size())))
            {
                if (packet.type() == Protocol::PacketType::Match)
                {
                    Protocol::Matchmaking match;
                    if (match.ParseFromString(packet.data()))
                    {
                        _roomId = match.roomid();
                        AddLog("[TCP] Match Packet: RoomID set to " + _roomId);
                    }
                }
            }

            AsyncReadTcp();
        }));
    }));
}

void DedicatedClient::SendIngamePacket(Protocol::IngameType type, const std::string& data)
{
    if (!_isIngame || _serverUdpPort == 0)
    {
        AddLog("[Ingame] Cannot send packet: not ingame ready");
        return;
    }

    Protocol::IngamePacket ingame;
    ingame.set_roomid(_roomId.empty() ? "DedicatedRoom" : _roomId);
    ingame.set_sessionid(_sessionId);
    ingame.set_method(type);
    ingame.set_data(data);
    ingame.set_clienttick(0);

    std::string serializedIngame;
    ingame.SerializeToString(&serializedIngame);

    Protocol::NetworkPacket packet;
    packet.set_type(Protocol::PacketType::Ingame);
    packet.set_data(serializedIngame);

    std::string serializedPacket;
    packet.SerializeToString(&serializedPacket);

    uint16_t totalSize = static_cast<uint16_t>(serializedPacket.size());
    uint16_t netSize = htons(totalSize);

    auto sendBuffer = std::make_shared<std::vector<uint8_t>>(2 + totalSize);
    std::memcpy(sendBuffer->data(), &netSize, 2);
    std::memcpy(sendBuffer->data() + 2, serializedPacket.data(), totalSize);

    asio::ip::udp::endpoint serverEndpoint(_serverAddress, _serverUdpPort);
    auto self = shared_from_this();

    _udpSocket.async_send_to(asio::buffer(*sendBuffer), serverEndpoint, asio::bind_executor(_strand, [this, self, sendBuffer, type](std::error_code ec, std::size_t) {
        if (ec)
            AddLog(std::format("[UDP] Send Ingame Packet failed: {}", ec.message()));
        else
            AddLog(std::format("[UDP] Sent Ingame Packet (Type: {})", static_cast<int>(type)));
    }));
}

void DedicatedClient::StartRandomInput(int intervalMs)
{
    if (!_isIngame)
    {
        AddLog("[RandomInput] Cannot start: not in ingame state yet");
        return;
    }

    if (_randomInputActive.exchange(true))
        return; // already active

    _randomPacketsSent = 0;
    AddLog(std::format("[RandomInput] Started streaming random input every {}ms", intervalMs));

    asio::post(_strand, [this, self = shared_from_this(), intervalMs]() {
        ScheduleRandomInput(intervalMs);
    });
}

void DedicatedClient::StopRandomInput()
{
    if (_randomInputActive.exchange(false))
    {
        asio::post(_strand, [this, self = shared_from_this()]() {
            if (_randomInputTimer)
            {
                std::error_code ec;
                _randomInputTimer->cancel(ec);
            }
        });
        AddLog(std::format("[RandomInput] Stopped. Total packets sent: {}", _randomPacketsSent.load()));
    }
}

void DedicatedClient::ScheduleRandomInput(int intervalMs)
{
    if (!_randomInputActive || !_isIngame)
        return;

    auto self = shared_from_this();
    _randomInputTimer->expires_after(std::chrono::milliseconds(intervalMs));
    _randomInputTimer->async_wait(asio::bind_executor(_strand, [this, self, intervalMs](std::error_code ec) {
        if (!ec)
        {
            if (_randomInputActive && _isIngame)
            {
                SendRandomInputPacket();
                ScheduleRandomInput(intervalMs);
            }
        }
        else if (ec != asio::error::operation_aborted)
        {
            AddLog("[RandomInput] Timer error: " + ec.message());
        }
    }));
}

void DedicatedClient::SendRandomInputPacket()
{
    // Generate random move direction
    float dirX = (static_cast<float>(rand() % 200) - 100.0f) / 100.0f;
    float dirZ = (static_cast<float>(rand() % 200) - 100.0f) / 100.0f;
    float posX = static_cast<float>(rand() % 100 - 50);
    float posZ = static_cast<float>(rand() % 100 - 50);

    Protocol::MovePacket move;
    move.set_playerid(_sessionId);
    move.set_originx(posX);
    move.set_originy(0.0f);
    move.set_originz(posZ);
    move.set_dirx(dirX);
    move.set_diry(0.0f);
    move.set_dirz(dirZ);

    std::string moveSerialized;
    if (move.SerializeToString(&moveSerialized))
    {
        SendIngamePacket(Protocol::IngameType::Move, moveSerialized);
    }

    // Intermittently (1 in 5 times) send Shoot packet
    if (rand() % 5 == 0)
    {
        SendIngamePacket(Protocol::IngameType::Shoot, "");
    }

    _randomPacketsSent++;
}
