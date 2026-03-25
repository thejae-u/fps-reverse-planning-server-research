#include "NetworkClient.hpp"
#include "IOManager.hpp"

NetworkClient::NetworkClient(std::shared_ptr<IOManager> ioManager)
    : _ioManager(ioManager), 
      _strand(asio::make_strand(ioManager->GetIoContext())),
      _socket(std::make_shared<asio::ip::tcp::socket>(_strand)), 
      _udpSocket(_strand)
{
    InitUdpSocket();
}

void NetworkClient::InitUdpSocket()
{
    std::error_code ec;
    if (_udpSocket.is_open()) {
        _udpSocket.close(ec);
    }
    _udpSocket.open(asio::ip::udp::v4(), ec);
    if (ec) {
        spdlog::error("Failed to open UDP socket: {}", ec.message());
    } else {
        _udpSocket.set_option(asio::socket_base::reuse_address(true));
        _udpSocket.bind(asio::ip::udp::endpoint(asio::ip::udp::v4(), 0), ec);
        if (ec) {
            spdlog::error("Failed to bind UDP socket: {}", ec.message());
        } else {
            _clientUdpPort = _udpSocket.local_endpoint().port();
            spdlog::info("Client UDP socket initialized on port {}", _clientUdpPort);
        }
    }
}

NetworkClient::~NetworkClient()
{
    Disconnect();
}

void NetworkClient::Connect(const std::string& host, uint16_t port)
{
    if(_connected)
        return;

    _serverHost = host;

    try
    {
        asio::ip::tcp::resolver resolver(_ioManager->GetIoContext());
        auto endpoints = resolver.resolve(host, std::to_string(port));
        auto self = shared_from_this();

        // Use socket's member async_connect
        _socket->async_connect(endpoints->endpoint(), asio::bind_executor(_strand, [this, self](std::error_code ec) {
            if(!ec)
            {
                _connected = true;
                // Get the actual address from the socket's remote endpoint
                std::error_code rec;
                auto endpoint = _socket->remote_endpoint(rec);
                if (!rec) {
                    _serverAddress = endpoint.address();
                    AddLog("Connected to " + endpoint.address().to_string() + ":" + std::to_string(endpoint.port()));
                }

                AsyncHandshake();
            }
            else
            {
                AddLog("Connection failed: " + ec.message(), spdlog::level::err);
            }
        }));

        AsyncReadUdp();
    }
    catch(std::exception& e)
    {
        AddLog(std::string("Connect error: ") + e.what(), spdlog::level::err);
    }
}

void NetworkClient::Disconnect()
{
    _connected = false;
    _isMatching = false;
    _roomId.clear();
    _sessionId.clear();
    _serverUdpPort = 0;

    std::error_code ec;
    if (_socket->is_open())
        _socket->close(ec);

    // Re-initialize sockets using member functions
    _socket = std::make_shared<asio::ip::tcp::socket>(_ioManager->GetIoContext());
    InitUdpSocket();

    AddLog("Disconnected. UDP Port re-initialized: " + std::to_string(_clientUdpPort));
}

void NetworkClient::SendMatchRequest()
{
    if(!_connected)
        return;

    if (_roomId.empty())
    {
        AddLog("Matchmaking requested (automatic)...");
        _isMatching = true;
    }
    else
    {
        AddLog("Matchmaking request ignored: already matched.");
    }
}

void NetworkClient::AsyncHandshake()
{
    auto self = shared_from_this();
    AddLog("Async Handshake started...");

    // 1. Receive server's UDP port size (2 bytes)
    auto netSize = std::make_shared<uint16_t>(0);
    asio::async_read(*_socket, asio::buffer(netSize.get(), sizeof(uint16_t)), asio::bind_executor(_strand, [this, self, netSize](std::error_code ec, std::size_t) {
        if(ec)
        {
            AddLog("Handshake read size error: " + ec.message(), spdlog::level::err);
            Disconnect();
            return;
        }

        uint16_t size = ntohs(*netSize);
        auto receiveBuffer = std::make_shared<std::vector<unsigned char>>(size);

        // 2. Receive server's real data
        asio::async_read(*_socket, asio::buffer(*receiveBuffer), asio::bind_executor(_strand, [this, self, receiveBuffer, size](std::error_code ec, std::size_t) {
            if(ec)
            {
                AddLog("Handshake read data error: " + ec.message(), spdlog::level::err);
                Disconnect();
                return;
            }

            Packet receivePacket;
            if(!receivePacket.ParseFromArray(receiveBuffer->data(), static_cast<int>(size)))
            {
                AddLog("Handshake error: Failed to parse server packet", spdlog::level::err);
                Disconnect();
                return;
            }

            if(receivePacket.type() != PacketType::PortHandshake)
            {
                AddLog("Handshake error: Unexpected packet type", spdlog::level::err);
                Disconnect();
                return;
            }

            std::string data = receivePacket.data();
            size_t commaPos = data.find(',');
            if (commaPos != std::string::npos)
            {
                _serverUdpPort = static_cast<uint16_t>(std::stoi(data.substr(0, commaPos)));
                _sessionId = data.substr(commaPos + 1);
                AddLog(std::format("Received Port: {}, SessionID: {}", _serverUdpPort, _sessionId));
            }
            else
            {
                _serverUdpPort = static_cast<uint16_t>(std::stoi(data));
                AddLog("Received Server UDP Port: " + std::to_string(_serverUdpPort) + " (Warning: No SessionID)");
            }

            // In the new sequence, we must perform UDP hole punching immediately 
            // after receiving the port and session ID to enter the matchmaking queue.
            AddLog("Handshake success! Sending UDP Hole Punching for matchmaking...");
            SendUdpHolePunching();
            
            AsyncRead();
        }));
    }));
}

void NetworkClient::Send(const std::string& message)
{
    if(!_connected)
        return;

    uint16_t size = static_cast<uint16_t>(message.size());
    uint16_t netSize = htons(size);
    auto buffer = std::make_shared<std::vector<char>>(sizeof(netSize) + message.size());
    std::memcpy(buffer->data(), &netSize, sizeof(netSize));
    std::memcpy(buffer->data() + sizeof(netSize), message.data(), message.size());

    auto self = shared_from_this();
    asio::async_write(*_socket, asio::buffer(*buffer), [this, self, buffer](std::error_code ec, std::size_t length) {
        if (ec) {
            AddLog("Write error: " + ec.message(), spdlog::level::err);
        }
    });
}

void NetworkClient::SendIngamePacket(IngameType type, const std::string& data)
{
    if (!_connected || _roomId.empty() || _sessionId.empty())
    {
        return;
    }

    try
    {
        IngamePacket ingame;
        ingame.set_roomid(_roomId);
        ingame.set_sessionid(_sessionId);
        ingame.set_method(type);
        ingame.set_data(data);

        std::string ingameData;
        if (!ingame.SerializeToString(&ingameData)) return;

        Packet packet;
        packet.set_type(PacketType::Ingame);
        packet.set_data(ingameData);

        std::string packetData;
        if (!packet.SerializeToString(&packetData)) return;

        asio::ip::udp::endpoint destination(_serverAddress, _serverUdpPort);
        
        std::uint16_t size = htons(static_cast<std::uint16_t>(packetData.size()));
        std::vector<char> buffer(sizeof(size) + packetData.size());
        std::memcpy(buffer.data(), &size, sizeof(size));
        std::memcpy(buffer.data() + sizeof(size), packetData.data(), packetData.size());

        _udpSocket.async_send_to(asio::buffer(buffer), destination, [this, buffer](std::error_code ec, std::size_t) {
            if (ec && ec != asio::error::operation_aborted)
            {
                AddLog("UDP Send error: " + ec.message(), spdlog::level::err);
            }
        });
    }
    catch (const std::exception& e)
    {
        AddLog(std::format("SendIngamePacket exception: {}", e.what()), spdlog::level::err);
    }
}

void NetworkClient::SendUdpCorrect(const std::string& message, const std::string& host, uint16_t port)
{
    try
    {
        AsyncReadUdp();

        asio::ip::udp::resolver resolver(_ioManager->GetIoContext());
        asio::ip::udp::endpoint destination = *resolver.resolve(asio::ip::udp::v4(), host, std::to_string(port)).begin();

        std::uint16_t size = htons(static_cast<std::uint16_t>(message.size()));
        std::vector<char> buffer(sizeof(size) + message.size());
        std::memcpy(buffer.data(), &size, sizeof(size));
        std::memcpy(buffer.data() + sizeof(size), message.data(), message.size());

        _udpSocket.async_send_to(asio::buffer(buffer), destination, [this, buffer, destination, message](std::error_code ec, std::size_t /*length*/) {
            if(ec)
            {
                AddLog("UDP Send error: " + ec.message(), spdlog::level::err);
            }

            AddLog("UDP Send Success: " + message);
        });
    }
    catch(std::exception& e)
    {
        AddLog(std::string("UDP Send exception: ") + e.what(), spdlog::level::err);
    }
}

void NetworkClient::SendUdpMalformed(const std::string& message, const std::string& host, uint16_t port, int errorType)
{
    try
    {
        AsyncReadUdp();

        asio::ip::udp::resolver resolver(_ioManager->GetIoContext());
        asio::ip::udp::endpoint destination = *resolver.resolve(asio::ip::udp::v4(), host, std::to_string(port)).begin();

        std::vector<char> buffer;
        if(errorType == 1)
        { // Size < 2 bytes
            buffer.push_back('X');
            AddLog("Sending malformed UDP: Size < 2 bytes", spdlog::level::warn);
        }
        else if(errorType == 2)
        {                                                                                     // Mismatched header
            std::uint16_t wrongSize = htons(static_cast<std::uint16_t>(message.size() + 10)); // Say it's 10 bytes longer
            buffer.resize(sizeof(wrongSize) + message.size());
            std::memcpy(buffer.data(), &wrongSize, sizeof(wrongSize));
            std::memcpy(buffer.data() + sizeof(wrongSize), message.data(), message.size());
            AddLog("Sending malformed UDP: Mismatched header (expected size != real size)", spdlog::level::warn);
        }

        _udpSocket.async_send_to(asio::buffer(buffer), destination, [this, buffer, destination](std::error_code ec, std::size_t /*length*/) {
            if(ec)
            {
                AddLog("UDP Send error: " + ec.message(), spdlog::level::err);
            }
        });
    }
    catch(std::exception& e)
    {
        AddLog(std::string("UDP Send exception: ") + e.what(), spdlog::level::err);
    }
}

void NetworkClient::AsyncRead()
{
    // 1. Read Header (2 bytes)
    auto self = shared_from_this();
    asio::async_read(*_socket, asio::buffer(&_readNetSize, sizeof(_readNetSize)), asio::bind_executor(_strand, [this, self](std::error_code ec, std::size_t length) {
        if(!ec)
        {
            _readSize = ntohs(_readNetSize);
            
            // 2. Read Body
            auto bodyBuffer = std::make_shared<std::vector<char>>(_readSize);
            asio::async_read(*_socket, asio::buffer(*bodyBuffer), asio::bind_executor(_strand, [this, self, bodyBuffer](std::error_code body_ec, std::size_t body_length) {
                if (!body_ec) {
                    Packet packet;
                    if (packet.ParseFromArray(bodyBuffer->data(), static_cast<int>(bodyBuffer->size()))) {
                        if (packet.type() == PacketType::InfoHandshake)
                        {
                            std::string info = packet.data();
                            size_t commaPos = info.find(',');
                            if (commaPos != std::string::npos)
                            {
                                _roomId = info.substr(0, commaPos);
                                // Session ID is already confirmed during PortHandshake
                                _isMatching = false;
                                AddLog("Matching complete! Entering Room: " + _roomId);
                            }
                        }
                        else if (packet.type() == PacketType::Ingame)
                        {
                            AddLog("Warning: Received Ingame packet via TCP. Ingame packets should be UDP only.", spdlog::level::warn);
                        }
                        else if (_messageCallback)
                        {
                            _messageCallback(packet.data());
                        }
                    }
                    AsyncRead(); // Read next
                } else {
                    if (body_ec == asio::error::eof)
                        AddLog("Server closed connection (EOF during body)", spdlog::level::warn);
                    else
                        AddLog("Read error (body): " + body_ec.message(), spdlog::level::err);
                    Disconnect();
                }
            }));
        }
        else if(_connected)
        {
            if (ec == asio::error::eof)
                AddLog("Server closed connection (EOF during header)", spdlog::level::warn);
            else if (ec != asio::error::operation_aborted)
                AddLog("Read error (header): " + ec.message(), spdlog::level::err);
            Disconnect();
        }
    }));
}

void NetworkClient::AsyncReadUdp()
{
    auto self = shared_from_this();
    _udpSocket.async_receive_from(asio::buffer(_udpReceiveBuffer), _udpRemoteEndpoint, [this, self](std::error_code ec, std::size_t length) {
        if(!ec)
        {
            // 서버의 UDP 패킷 포맷: [2바이트 크기(BigEndian)] + [Protobuf 데이터]
            if (length >= 2)
            {
                uint16_t size;
                std::memcpy(&size, _udpReceiveBuffer.data(), sizeof(size));
                size = ntohs(size);

                // 실제 수신된 데이터 길이가 헤더에 명시된 크기와 일치하는지 검증
                if (length >= static_cast<size_t>(2 + size))
                {
                    Packet packet;
                    if (packet.ParseFromArray(_udpReceiveBuffer.data() + 2, size))
                    {
                        if (packet.type() == PacketType::Ingame)
                        {
                            IngamePacket ingame;
                            if (ingame.ParseFromString(packet.data()))
                            {
                                std::string typeStr;
                                switch (ingame.method())
                                {
                                case IngameType::IngameOk: typeStr = "IngameOk"; break;
                                case IngameType::Move: typeStr = "Move"; break;
                                case IngameType::Jump: typeStr = "Jump"; break;
                                case IngameType::Shoot: typeStr = "Shoot"; break;
                                case IngameType::Hit: typeStr = "Hit"; break;
                                default: typeStr = "Unknown"; break;
                                }
                                
                                AddLog(std::format("[UDP Ingame] Room: {}, Session: {}, Type: {}", 
                                    ingame.roomid().substr(0, 8), ingame.sessionid().substr(0, 8), typeStr));
                                
                                if (_udpMessageCallback)
                                    _udpMessageCallback(std::format("UDP_INGAME: {} from {}", typeStr, ingame.sessionid().substr(0, 8)));
                            }
                        }
                        else
                        {
                            if (_udpMessageCallback)
                                _udpMessageCallback("UDP Control: " + std::to_string(static_cast<int>(packet.type())));
                        }
                    }
                    else
                    {
                        AddLog("UDP Parse Error: Failed to parse Packet protobuf", spdlog::level::warn);
                    }
                }
                else
                {
                    AddLog(std::format("UDP Size Mismatch: Expected {}, Received {}", size + 2, length), spdlog::level::warn);
                }
            }
            else if (length > 0)
            {
                AddLog("UDP Packet too small (under 2 bytes)", spdlog::level::warn);
            }

            AsyncReadUdp(); // 계속해서 수신 대기
        }
        else if(ec != asio::error::operation_aborted)
        {
            AddLog("UDP Read error: " + ec.message(), spdlog::level::err);
            // 에러 발생 시 잠시 대기 후 다시 수신 시도 (무한 루프 방지)
            auto timer = std::make_shared<asio::steady_timer>(_ioManager->GetIoContext(), std::chrono::milliseconds(100));
            timer->async_wait([this, self, timer](const std::error_code& /*teck*/) {
                AsyncReadUdp();
            });
        }
    });
}

void NetworkClient::AddLog(const std::string& msg, spdlog::level::level_enum level)
{
    std::lock_guard<std::mutex> lock(_logMutex);
    _logs.push_back({ msg, level });
    if(_logs.size() > 100)
    {
        _logs.pop_front();
    }
    spdlog::log(level, msg);
}

void NetworkClient::SetMessageCallback(MessageCallback callback)
{
    _messageCallback = std::move(callback);
}

void NetworkClient::SetUdpMessageCallback(MessageCallback callback)
{
    _udpMessageCallback = std::move(callback);
}

void NetworkClient::SendUdpHolePunching()
{
    if (!_connected || _sessionId.empty())
    {
        return;
    }

    try
    {
        AuthenticationPacket auth;
        // Room ID may be empty during the initial matchmaking authentication phase
        auth.set_roomid(_roomId);
        auth.set_sessionid(_sessionId);
        auth.set_method(AuthenticationType::UdpHolePunching);

        std::string authData;
        if (!auth.SerializeToString(&authData)) return;

        Packet packet;
        packet.set_type(PacketType::Autentication);
        packet.set_data(authData);

        std::string packetData;
        if (!packet.SerializeToString(&packetData)) return;

        asio::ip::udp::endpoint destination(_serverAddress, _serverUdpPort);
        
        std::uint16_t size = htons(static_cast<std::uint16_t>(packetData.size()));
        std::vector<char> buffer(sizeof(size) + packetData.size());
        std::memcpy(buffer.data(), &size, sizeof(size));
        std::memcpy(buffer.data() + sizeof(size), packetData.data(), packetData.size());

        // Send a few times to increase chance of success
        for (int i = 0; i < 5; ++i)
        {
            auto buffer_ptr = std::make_shared<std::vector<char>>(buffer);
            _udpSocket.async_send_to(asio::buffer(*buffer_ptr), destination, [this, buffer_ptr](std::error_code ec, std::size_t) {
                if (ec && ec != asio::error::operation_aborted)
                {
                    AddLog("UDP Hole Punching error: " + ec.message(), spdlog::level::err);
                }
            });
        }
        
        AddLog("Sent UDP Hole Punching packets to " + destination.address().to_string() + ":" + std::to_string(destination.port()));
    }
    catch (const std::exception& e)
    {
        AddLog(std::format("SendUdpHolePunching exception: {}", e.what()), spdlog::level::err);
    }
}
