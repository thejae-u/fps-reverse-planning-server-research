#include "NetworkClient.hpp"

NetworkClient::NetworkClient() : _socket(std::make_shared<asio::ip::tcp::socket>(_ioContext)), _udpSocket(_ioContext)
{
    std::error_code ec;
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

void NetworkClient::EnsureIOThreadStarted()
{
    if(_contextThread)
        return;

    _contextThread = std::make_unique<std::thread>([this]() {
        try
        {
            asio::io_context::work work(_ioContext);
            _ioContext.run();
        }
        catch(std::exception& e)
        {
            AddLog(std::string("IO Context error: ") + e.what(), spdlog::level::err);
        }
    });
}

void NetworkClient::Connect(const std::string& host, uint16_t port)
{
    if(_connected)
        return;

    try
    {
        asio::ip::tcp::resolver resolver(_ioContext);
        auto endpoints = resolver.resolve(host, std::to_string(port));

        asio::async_connect(*_socket, endpoints, [this](std::error_code ec, asio::ip::tcp::endpoint endpoint) {
            if(!ec)
            {
                _connected = true;
                AddLog("Connected to " + endpoint.address().to_string() + ":" + std::to_string(endpoint.port()));

                // Start Handshake instead of AsyncRead directly
                std::thread([this]() {
                    Handshake();
                })
                .detach();
            }
            else
            {
                AddLog("Connection failed: " + ec.message(), spdlog::level::err);
            }
        });

        AsyncReadUdp();
        EnsureIOThreadStarted();
    }
    catch(std::exception& e)
    {
        AddLog(std::string("Connect error: ") + e.what(), spdlog::level::err);
    }
}

void NetworkClient::Disconnect()
{
    _connected = false;
    _ioContext.stop();
    if(_contextThread && _contextThread->joinable())
    {
        _contextThread->join();
    }
    _contextThread.reset();
    _ioContext.reset();

    std::error_code ec;
    _socket->close(ec);
    _udpSocket.close(ec);

    // Create new sockets for next connection
    _socket = std::make_shared<asio::ip::tcp::socket>(_ioContext);
    
    // Re-initialize UDP socket
    _udpSocket = asio::ip::udp::socket(_ioContext);
    _udpSocket.open(asio::ip::udp::v4(), ec);
    if (!ec) {
        _udpSocket.set_option(asio::socket_base::reuse_address(true));
        _udpSocket.bind(asio::ip::udp::endpoint(asio::ip::udp::v4(), 0), ec);
        if (!ec) {
            _clientUdpPort = _udpSocket.local_endpoint().port();
        }
    }
    _serverUdpPort = 0;
    _roomId.clear();
    _sessionId.clear();
    _isMatching = false;

    AddLog("Disconnected. Re-initialized UDP Port: " + std::to_string(_clientUdpPort));
}

void NetworkClient::Send(const std::string& message)
{
    if(!_connected)
        return;

    uint16_t size = static_cast<uint16_t>(message.size());
    uint16_t netSize = htons(size);
    std::vector<char> buffer(sizeof(netSize) + message.size());
    std::memcpy(buffer.data(), &netSize, sizeof(netSize));
    std::memcpy(buffer.data() + sizeof(netSize), message.data(), message.size());

    asio::async_write(*_socket, asio::buffer(buffer), [this, buffer](std::error_code ec, std::size_t /*length*/) {
        if(ec)
        {
            AddLog("Write error: " + ec.message(), spdlog::level::err);
        }
    });
}

void NetworkClient::SendUdpCorrect(const std::string& message, const std::string& host, uint16_t port)
{
    try
    {
        EnsureIOThreadStarted();
        AsyncReadUdp();

        asio::ip::udp::resolver resolver(_ioContext);
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
        EnsureIOThreadStarted();
        AsyncReadUdp();

        asio::ip::udp::resolver resolver(_ioContext);
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

void NetworkClient::SetMessageCallback(MessageCallback callback)
{
    _messageCallback = std::move(callback);
}

void NetworkClient::SetUdpMessageCallback(MessageCallback callback)
{
    _udpMessageCallback = std::move(callback);
}

void NetworkClient::AsyncRead()
{
    // Read the size of the message (2 bytes for uint16_t with ntohs)
    asio::async_read(*_socket, asio::buffer(&_readNetSize, sizeof(_readNetSize)), [this](std::error_code ec, std::size_t /*length*/) {
        if(!ec)
        {
            _readSize = ntohs(_readNetSize);

            // Now read the message content
            asio::async_read(*_socket, _readBuffer, asio::transfer_exactly(_readSize), [this](std::error_code ec, std::size_t length) {
                if(!ec)
                {
                    std::string data(asio::buffers_begin(_readBuffer.data()), asio::buffers_begin(_readBuffer.data()) + length);
                    _readBuffer.consume(length);

                    Packet packet;
                    if (packet.ParseFromString(data))
                    {
                        if (packet.type() == PacketType::InfoHandshake)
                        {
                            // Parse comma-separated "roomId,sessionId"
                            std::string info = packet.data();
                            size_t commaPos = info.find(',');
                            if (commaPos != std::string::npos)
                            {
                                _roomId = info.substr(0, commaPos);
                                _sessionId = info.substr(commaPos + 1);
                                _isMatching = false;
                                AddLog("Matching complete! Room: " + _roomId + ", Session: " + _sessionId);
                            }
                        }

                        if (_messageCallback)
                        {
                            // For backward compatibility with UI displaying text, we can convert back or handle differently
                            // For now, let's just pass the data or some representation
                            _messageCallback(packet.data());
                        }
                    }
                    else
                    {
                        // Fallback if not a protobuf packet (e.g. raw text from some tests)
                        if (_messageCallback)
                        {
                            _messageCallback(data);
                        }
                    }
                    
                    AsyncRead(); // Read the next message
                }
                else
                {
                    AddLog("Read error: " + ec.message(), spdlog::level::err);
                    Disconnect();
                }
            });
        }
        else
        {
            if(_connected)
            {
                AddLog("Read error (size): " + ec.message(), spdlog::level::err);
                Disconnect();
            }
        }
    });
}

void NetworkClient::AsyncReadUdp()
{
    _udpSocket.async_receive_from(asio::buffer(_udpReceiveBuffer), _udpRemoteEndpoint, [this](std::error_code ec, std::size_t length) {
        if(!ec)
        {
            std::string message(_udpReceiveBuffer.data(), length);
            if(_udpMessageCallback)
            {
                _udpMessageCallback(message);
            }
            AsyncReadUdp();
        }
        else if(ec != asio::error::operation_aborted)
        {
            AddLog("UDP Read error: " + ec.message(), spdlog::level::err);
            AsyncReadUdp();
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

void NetworkClient::Handshake()
{
    AddLog("Handshake started...");

    std::error_code ec;
    
    // Ensure UDP socket is bound and we have the port
    if (!_udpSocket.is_open()) {
        _udpSocket.open(asio::ip::udp::v4(), ec);
    }
    
    _clientUdpPort = _udpSocket.local_endpoint().port();
    if (_clientUdpPort == 0) {
        _udpSocket.bind(asio::ip::udp::endpoint(asio::ip::udp::v4(), 0), ec);
        _clientUdpPort = _udpSocket.local_endpoint().port();
    }
    
    AddLog("Client UDP Port for handshake: " + std::to_string(_clientUdpPort));

    // 1. Receive server's UDP port
    uint16_t netSize;
    asio::read(*_socket, asio::buffer(&netSize, sizeof(netSize)), ec);
    if(ec)
    {
        AddLog("Handshake error (receive size): " + ec.message(), spdlog::level::err);
        Disconnect();
        return;
    }

    uint16_t size = ntohs(netSize);
    std::vector<char> receiveData(size);
    asio::read(*_socket, asio::buffer(receiveData), ec);
    if(ec)
    {
        AddLog("Handshake error (receive data): " + ec.message(), spdlog::level::err);
        Disconnect();
        return;
    }

    Packet receivePacket;
    if(!receivePacket.ParseFromArray(receiveData.data(), static_cast<int>(size)))
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

    _serverUdpPort = static_cast<uint16_t>(std::stoi(receivePacket.data()));
    AddLog("Received Server UDP Port: " + std::to_string(_serverUdpPort));

    // 2. Send client's UDP port
    Packet sendPacket;
    sendPacket.set_type(PacketType::PortHandshake);
    sendPacket.set_data(std::to_string(_clientUdpPort));

    std::string sendData;
    sendPacket.SerializeToString(&sendData);

    uint16_t sendNetSize = htons(static_cast<uint16_t>(sendData.size()));
    asio::write(*_socket, asio::buffer(&sendNetSize, sizeof(sendNetSize)), ec);
    if(!ec)
    {
        asio::write(*_socket, asio::buffer(sendData), ec);
    }

    if(ec)
    {
        AddLog("Handshake error (send): " + ec.message(), spdlog::level::err);
        Disconnect();
        return;
    }

    AddLog("Handshake success! Client UDP Port: " + std::to_string(_clientUdpPort));

    // 3. Start normal async read
    AsyncRead();
}
