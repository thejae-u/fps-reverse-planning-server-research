#include "Server.hpp"

#include "IOManager.hpp"
#include "Matching.hpp"
#include "Room.hpp"
#include "Session.hpp"

Server::Server(SecretKey, std::shared_ptr<IOManager> ioManager, std::shared_ptr<Matching> matching, std::uint16_t port)
: _ioManager(ioManager), _matching(matching), _tcpEndpoint(asio::ip::tcp::v4(), port),
  _acceptor(ioManager->GetIoContext(), _tcpEndpoint), _udpSocket(ioManager->GetIoContext(), asio::ip::udp::endpoint(asio::ip::udp::v4(), 0))
{
    _udpEndpoint = _udpSocket.local_endpoint();
    spdlog::info("server object created: tcp port {}, udp port {}", _tcpEndpoint.port(), _udpEndpoint.port());
}

void Server::Start()
{
    spdlog::info("server started...");

    _matching->SetRegisterRoomCallback([weakSelf = weak_from_this()](const std::shared_ptr<Room>& room) {
        if(auto self = weakSelf.lock())
            self->AddRoom(room);
    });

    _matching->SetRemoveRoomCallback([weakSelf = weak_from_this()](const std::shared_ptr<Room>& room) {
        if(auto self = weakSelf.lock())
            self->RemoveRoom(room);
    });

    AcceptAsync();
    ReceiveAsyncByUdp();

    _matching->Start();
}

void Server::Stop()
{
    _matching->Stop();
    _rooms.clear();
    _acceptor.close();
    _udpSocket.close();
    spdlog::info("server stoped...\n");
}

void Server::AcceptAsync()
{
    auto weakSelf(weak_from_this());
    auto newSession = Session::Create(_ioManager, _uuidGen(), _udpEndpoint.port());

    // async accept new client
    _acceptor.async_accept(*newSession->GetSocket(), [weakSelf, newSession](std::error_code ec) {
        if(ec)
        {
            if(ec == asio::error::connection_aborted ||
               ec == asio::error::operation_aborted)
            {
                spdlog::info("server: acceptor aborted");
                return;
            }

            spdlog::error("server: accept error occured({})", ec.message());
            return;
        }

        // session information
        auto sessionAddrStr = newSession->GetEndpoint().address().to_string();
        auto sessionId = newSession->GetId();

        if(auto self = weakSelf.lock())
        {
            std::lock_guard<std::mutex> sessionsLock(self->_sessionsMutex);
            if(self->_sessions.find(sessionId) != self->_sessions.end())
            {
                spdlog::error("server: invalid session id {} is already exists", uuids::to_string(sessionId));
                self->AcceptAsync();
                return;
            }

            // add to matchmaking queue
            // move session ownership to Matching 
            self->_matching->AddWaitSession(sessionId, std::move(newSession));

            // new session create for accept other client
            self->AcceptAsync();
        }
    });
}

void Server::ReceiveAsyncByUdp()
{
    auto receiveBuffer = std::make_shared<std::vector<unsigned char>>(BUF_SIZE);
    auto senderEndpoint = std::make_shared<asio::ip::udp::endpoint>();
    _udpSocket.async_receive_from(asio::buffer(*receiveBuffer), *senderEndpoint, [weakSelf = weak_from_this(), receiveBuffer, senderEndpoint](const std::error_code ec, const std::size_t bytesRead) {
        if(ec)
        {
            if(ec == asio::error::operation_aborted)
            {
                spdlog::info("server: udp socket close complete");
                return;
            }

            spdlog::warn("server: udp error occurred({})", ec.message());
            if(auto self = weakSelf.lock())
            {
                self->ReceiveAsyncByUdp();
                return;
            }
        }

        if(bytesRead < sizeof(std::uint16_t))
        {
            spdlog::error("server: bad size received (size {})", bytesRead);
            if(auto self = weakSelf.lock())
                self->ReceiveAsyncByUdp();

            return;
        }

        // first 2 bytes are data length header
        std::uint16_t expectedSize;
        std::memcpy(&expectedSize, receiveBuffer->data(), sizeof(expectedSize));
        expectedSize = ntohs(expectedSize);

        std::size_t realSize = bytesRead - sizeof(std::uint16_t);

        if(expectedSize != realSize)
        {
            spdlog::error("server: bad data received (expected {}, real {})", expectedSize, realSize);
            if(auto self = weakSelf.lock())
                self->ReceiveAsyncByUdp();

            return;
        }

        const unsigned char* realData = receiveBuffer->data() + 2;

        // send to room
        // client must have own room id and session id
        if(auto self = weakSelf.lock())
        {
            self->_ioManager->RegisterAsyncWork([weakSelf, receiveBuffer, realData, realSize]() {
                if(auto self = weakSelf.lock())
                    self->ProcessPacketAsync(realSize, realData);
            });
        }

        if(auto self = weakSelf.lock())
            self->ReceiveAsyncByUdp();
    });
}

void Server::ProcessPacketAsync(std::uint16_t size, const unsigned char* data)
{
    Packet packet;
    if(!packet.ParseFromArray(data, size))
    {
        spdlog::error("server: parsing udp real data error");
        return;
    }

    if(packet.type() != PacketType::Ingame)
    {
        spdlog::error("server: invalid packet income");
        return;
    }

    auto ingamePacket = std::make_shared<IngamePacket>();
    if(!ingamePacket->ParseFromString(packet.data()))
    {
        spdlog::error("server: parsing ingame packet error");
        return;
    }

    auto roomId = uuids::uuid::from_string(ingamePacket->roomid());

    std::lock_guard<std::mutex> roomsLock(_roomsMutex);
    if(auto room = _rooms.find(roomId.value()); room != _rooms.end())
    {
        room->second->EnqueuePacket(std::move(ingamePacket));
        return;
    }

    spdlog::error("server: invalid room id ({})", ingamePacket->roomid());
    return;
}

void Server::AddRoom(std::shared_ptr<Room> room)
{
    std::lock_guard<std::mutex> roomsLock(_roomsMutex);
    auto roomId = room->GetId();
    _rooms[roomId] = room;

    spdlog::info("server: add room {} to server", uuids::to_string(roomId));
}

void Server::RemoveRoom(std::shared_ptr<Room> room)
{
    std::lock_guard<std::mutex> roomsLock(_roomsMutex);
    auto removeId = room->GetId();
    if(_rooms.find(removeId) != _rooms.end())
    {
        _rooms.erase(removeId);
    }

    spdlog::info("server: remove room {} from server", uuids::to_string(removeId));
}