#include "Server.hpp"

#include "IOManager.hpp"
#include "SessionManager.hpp"
#include "Matching.hpp"
#include "Room.hpp"
#include "Session.hpp"

Server::Server(SecretKey, std::shared_ptr<IOManager> ioManager, std::shared_ptr<SessionManager> sessionManager, std::shared_ptr<Matching> matching, std::uint16_t port)
: _ioManager(ioManager), _strand(ioManager->GetIoContext()), _sessionManager(sessionManager), _matching(matching), _tcpEndpoint(asio::ip::tcp::v4(), port),
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
    auto newSession = Session::Create(_ioManager, _uuidGen(), _udpEndpoint.port());
    std::lock_guard<std::mutex> sessionsLock(_sessionsMutex);
    auto sessionId = newSession->GetId();
    if(_sessions.find(sessionId) != _sessions.end())
    {
        spdlog::error("server: create duplicate session, create again");
        AcceptAsync();
        return;
    }

    newSession->AddDisconnectListener([weakSelf = weak_from_this(), sessionId](const std::weak_ptr<Session>& weakSession) {
        if(auto self = weakSelf.lock())
        {
            if(auto session = weakSession.lock())
            {
                std::lock_guard<std::mutex> sessionsLock(self->_sessionsMutex);
                auto it = self->_sessions.find(sessionId);
                self->_sessions.erase(it);
                spdlog::info("server: session {} removed from server", uuids::to_string(sessionId));
            }
        }
    });

    // Udp Send handler register
    newSession->SetSendToHandler([weakSelf = weak_from_this()](asio::ip::udp::endpoint ep, std::shared_ptr<Raw> data) {
        if(auto self = weakSelf.lock())
            self->EnqueueSendData(ep, std::move(data));
    });

    auto weakSession = _sessionManager->Insert(newSession->GetId(), std::move(newSession));
    _sessions.insert({ sessionId, weakSession });

    // async accept new client
    if(auto session = weakSession.lock())
    {
        _acceptor.async_accept(*session->GetSocket(), [weakSelf = weak_from_this(), weakSession, sessionId](const std::error_code& ec) {
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

            if(auto self = weakSelf.lock())
            {
                if(auto session = weakSession.lock())
                {
                    session->Init();

                    // new session create for accept other client
                    self->AcceptAsync();
                }
            }
        });
    }
}

void Server::EnqueueSendData(asio::ip::udp::endpoint ep, const std::shared_ptr<Raw> payload)
{
    {
        std::lock_guard<std::mutex> payloadQueueLock(_payloadQueueMutex);
        _payloadQueue.push({ ep, payload });
    }

    if(!_isSending)
    {
        SendAsyncByUdp();
    }
}

void Server::SendAsyncByUdp()
{
    std::lock_guard<std::mutex> payloadQueueLock(_payloadQueueMutex);
    auto [ep, payload] = _payloadQueue.front();
    _payloadQueue.pop();

    bool hasMore = !_payloadQueue.empty();

    _udpSocket.async_send_to(
    asio::buffer(*payload), ep, asio::bind_executor(_strand, [weakSelf = weak_from_this(), payload, ep, hasMore](const std::error_code& ec, std::size_t) {
        if(ec)
        {
            spdlog::error("server: udp send error occured({})", ec.message());
            return;
        }

        if(auto self = weakSelf.lock())
        {
            spdlog::info("[test log] server: udp send to ({}:{}) complete", ep.address().to_string(), ep.port());

            if(hasMore)
                self->SendAsyncByUdp();
        }
    }));
}

void Server::ReceiveAsyncByUdp()
{
    auto receiveBuffer = std::make_shared<std::vector<unsigned char>>(BUF_SIZE);
    auto senderEndpoint = std::make_shared<asio::ip::udp::endpoint>();
    _udpSocket.async_receive_from(
    asio::buffer(*receiveBuffer), *senderEndpoint, asio::bind_executor(_strand, [weakSelf = weak_from_this(), receiveBuffer, senderEndpoint](const std::error_code& ec, const std::size_t bytesRead) {
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

        std::size_t payloadSize = bytesRead - sizeof(std::uint16_t);

        if(expectedSize != payloadSize)
        {
            spdlog::error("server: bad data received (expected {}, real {})", expectedSize, payloadSize);
            if(auto self = weakSelf.lock())
                self->ReceiveAsyncByUdp();

            return;
        }

        // raw pointer to data payload, no onwership
        const unsigned char* payload = receiveBuffer->data() + 2;

        // send to room
        // client must have own room id and session id
        if(auto self = weakSelf.lock())
        {
            self->_ioManager->PostOnBlockingPool([weakSelf, senderEndpoint, receiveBuffer, payload, payloadSize]() {
                if(auto self = weakSelf.lock())
                    self->ProcessPacket(std::move(senderEndpoint), payloadSize, payload);
            });
        }

        if(auto self = weakSelf.lock())
            self->ReceiveAsyncByUdp();
    }));
}

void Server::ProcessPacket(std::shared_ptr<asio::ip::udp::endpoint> sender, std::uint16_t size, const unsigned char* data)
{
    Packet packet;
    if(!packet.ParseFromArray(data, size))
    {
        spdlog::error("server: parsing udp real data error");
        return;
    }

    if(packet.type() == PacketType::Ingame)
    {
        auto ingamePacket = std::make_shared<IngamePacket>();
        if(!ingamePacket->ParseFromString(packet.data()))
        {
            spdlog::error("server: parsing ingame packet error");
            return;
        }

        auto roomId = uuids::uuid::from_string(ingamePacket->roomid());
        auto room = GetRoom(roomId.value());
        if(room == nullptr)
        {
            spdlog::error("server: invalid room id ({})", ingamePacket->roomid());
            return;
        }

        room->EnqueuePacket(std::move(ingamePacket));
        return;
    }

    // Client UDP HolePunching
    if(packet.type() == PacketType::Autentication)
    {
        AuthenticationPacket authPacket;
        if(!authPacket.ParseFromString(packet.data()))
        {
            spdlog::error("server: parsing authentication packet error");
            return;
        }

        auto sessionId = uuids::uuid::from_string(authPacket.sessionid());
        if(!sessionId.has_value())
        {
            spdlog::error("server: auth packet has no session id");
            return;
        }

        // move to matching, wait for matchmaking
        {
            std::lock_guard<std::mutex> sessionsLock(_sessionsMutex);
            auto it = _sessions.find(sessionId.value());
            if(it == _sessions.end())
            {
                spdlog::error("server: no session in server");
                return;
            }

            auto& weakSession = it->second;
            if(auto session = weakSession.lock())
            {
                session->PunchUdpHole(*sender);

                // transfer session ownership from session to matching
                _matching->AddWaitSession(session->GetId(), weakSession);
            }
        }
    }
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