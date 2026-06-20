#include "Listener.hpp"

#include "IOManager.hpp"
#include "SessionManager.hpp"
#include "Matching.hpp"
#include "Room.hpp"
#include "Session.hpp"

Listener::Listener(SecretKey, std::shared_ptr<IOManager> ioManager, std::shared_ptr<SessionManager> sessionManager, std::shared_ptr<Matching> matching, std::uint16_t port)
: _ioManager(ioManager), _strand(ioManager->GetIoContext()), _sessionManager(sessionManager), _matching(matching), _tcpEndpoint(asio::ip::tcp::v4(), port),
  _acceptor(ioManager->GetIoContext(), _tcpEndpoint), _udpSocket(ioManager->GetIoContext(), asio::ip::udp::endpoint(asio::ip::udp::v4(), 0))
{
    _udpEndpoint = _udpSocket.local_endpoint();
    spdlog::info("listener object created: tcp port {}, udp port {}", _tcpEndpoint.port(), _udpEndpoint.port());
}

void Listener::Start()
{
    spdlog::info("listener started...");

    _matching->SetRegisterRoomCallback([weakSelf = GetWeak<Listener>()](const std::shared_ptr<Room>& room) {
        if(auto self = weakSelf.lock())
            self->AddRoom(room);
    });

    _matching->SetRemoveRoomCallback([weakSelf = GetWeak<Listener>()](const std::shared_ptr<Room>& room) {
        if(auto self = weakSelf.lock())
            self->RemoveRoom(room);
    });

    AcceptAsync();
    ReceiveAsyncByUdp();

    _matching->Start();
}

void Listener::Stop()
{
    _matching->Stop();
    _rooms.clear();
    _acceptor.close();
    _udpSocket.close();
    spdlog::info("listener stopped...\n");
}

void Listener::AcceptAsync()
{
    uuids::uuid sessionId;
    {
        std::lock_guard<std::mutex> lock(_uuidMutex);
        sessionId = _uuidGen();
    }
    auto newSession = Session::Create(_ioManager, GetWeak<Listener>(), sessionId, _udpEndpoint.port());

    std::lock_guard<std::mutex> sessionsLock(_sessionsMutex);
    if(_sessions.find(sessionId) != _sessions.end())
    {
        spdlog::error("listener: create duplicate session, create again");
        AcceptAsync();
        return;
    }

    auto handle = newSession->AddDisconnectCallback([weakSelf = GetWeak<Listener>(), sessionId](const std::shared_ptr<Session>& session) {
        if(auto self = weakSelf.lock())
        {
            std::lock_guard<std::mutex> sessionsLock(self->_sessionsMutex);
            self->_sessions.erase(sessionId);
            self->_sessionCallbackHandles.erase(sessionId);
            spdlog::info("listener: session {} removed from listener", uuids::to_string(sessionId));
        }
    });

    // Udp Send handler register
    newSession->SetSendToHandler([weakSelf = GetWeak<Listener>()](asio::ip::udp::endpoint ep, std::shared_ptr<Raw> data) {
        if(auto self = weakSelf.lock())
            self->EnqueueSendData(ep, std::move(data));
    });

    auto weakSession = _sessionManager->Insert(newSession->GetId(), std::move(newSession));

    _sessions[sessionId] = weakSession;
    _sessionCallbackHandles[sessionId] = handle;

    // async accept new client
    if(auto session = weakSession.lock())
    {
        _acceptor.async_accept(*session->GetSocket(), [weakSelf = GetWeak<Listener>(), weakSession, sessionId](const std::error_code& ec) {
            if(ec)
            {
                if(ec == asio::error::connection_aborted ||
                   ec == asio::error::operation_aborted)
                {
                    spdlog::info("listener: acceptor aborted");
                    return;
                }

                spdlog::error("listener: accept error occured({})", ec.message());
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

void Listener::EnqueueSendData(asio::ip::udp::endpoint ep, const std::shared_ptr<Raw> payload)
{
    {
        std::lock_guard<std::mutex> payloadQueueLock(_payloadQueueMutex);
        _payloadQueue.push({ ep, payload });
    }

    if(_isSending)
        return;

    _isSending = true;
    SendAsyncByUdp();
}

void Listener::SendAsyncByUdp()
{
    std::lock_guard<std::mutex> payloadQueueLock(_payloadQueueMutex);
    auto [ep, payload] = _payloadQueue.front();
    _payloadQueue.pop();

    _udpSocket.async_send_to(
    asio::buffer(*payload), ep, asio::bind_executor(_strand, [weakSelf = GetWeak<Listener>(), payload, ep](const std::error_code& ec, std::size_t) {
        if(ec)
        {
            spdlog::error("listener: udp send error occured({})", ec.message());
            return;
        }

        if(auto self = weakSelf.lock())
        {
            spdlog::info("[test log] listener: udp send to ({}:{}) complete", ep.address().to_string(), ep.port());

            std::lock_guard<std::mutex> payloadQueueLock(self->_payloadQueueMutex);
            if(self->_payloadQueue.empty())
            {
                self->_isSending = false;
                return;
            }

            self->SendAsyncByUdp();
        }
    }));
}

void Listener::ReceiveAsyncByUdp()
{
    auto receiveBuffer = std::make_shared<std::vector<unsigned char>>(BUF_SIZE);
    auto senderEndpoint = std::make_shared<asio::ip::udp::endpoint>();
    _udpSocket.async_receive_from(
    asio::buffer(*receiveBuffer), *senderEndpoint, asio::bind_executor(_strand, [weakSelf = GetWeak<Listener>(), receiveBuffer, senderEndpoint](const std::error_code& ec, const std::size_t bytesRead) {
        if(ec)
        {
            if(ec == asio::error::operation_aborted)
            {
                spdlog::info("listener: udp socket close complete");
                return;
            }

            spdlog::warn("listener: udp error occurred({})", ec.message());
            if(auto self = weakSelf.lock())
            {
                self->ReceiveAsyncByUdp();
                return;
            }
        }

        if(bytesRead < sizeof(std::uint16_t))
        {
            spdlog::error("listener: bad size received (size {})", bytesRead);
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
            spdlog::error("listener: bad data received (expected {}, real {})", expectedSize, payloadSize);
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

void Listener::ProcessPacket(std::shared_ptr<asio::ip::udp::endpoint> sender, std::uint16_t size, const unsigned char* data)
{
    Packet packet;
    if(!packet.ParseFromArray(data, size))
    {
        spdlog::error("listener: parsing udp real data error");
        return;
    }

    if(packet.type() == PacketType::Ingame)
    {
        auto ingamePacket = std::make_shared<IngamePacket>();
        if(!ingamePacket->ParseFromString(packet.data()))
        {
            spdlog::error("listener: parsing ingame packet error");
            return;
        }

        auto roomId = uuids::uuid::from_string(ingamePacket->roomid());
        auto room = GetRoom(roomId.value());
        if(room == nullptr)
        {
            spdlog::error("listener: invalid room id ({})", ingamePacket->roomid());
            return;
        }

        room->EnqueuePacket(std::move(ingamePacket));
        return;
    }

    // Client UDP HolePunching
    if(packet.type() == PacketType::Authentication)
    {
        AuthenticationPacket authPacket;
        if(!authPacket.ParseFromString(packet.data()))
        {
            spdlog::error("listener: parsing authentication packet error");
            return;
        }

        auto sessionId = uuids::uuid::from_string(authPacket.sessionid());
        if(!sessionId.has_value())
        {
            spdlog::error("listener: auth packet has no session id");
            return;
        }

        // move to matching, wait for matchmaking
        {
            std::lock_guard<std::mutex> sessionsLock(_sessionsMutex);
            auto it = _sessions.find(sessionId.value());
            if(it == _sessions.end())
            {
                spdlog::error("listener: no session in listener");
                return;
            }

            auto& weakSession = it->second;
            if(auto session = weakSession.lock())
            {
                session->PunchUdpHole(*sender);
            }
        }
    }
}

void Listener::AddRoom(std::shared_ptr<Room> room)
{
    std::lock_guard<std::mutex> roomsLock(_roomsMutex);
    auto roomId = room->GetId();
    _rooms[roomId] = room;

    spdlog::info("listener: add room {} to listener", uuids::to_string(roomId));
}

void Listener::RemoveRoom(std::shared_ptr<Room> room)
{
    std::lock_guard<std::mutex> roomsLock(_roomsMutex);
    auto removeId = room->GetId();
    if(_rooms.find(removeId) != _rooms.end())
    {
        _rooms.erase(removeId);
    }

    spdlog::info("listener: remove room {} from listener", uuids::to_string(removeId));
}

bool Listener::AddToMatchmakingQueue(uuids::uuid sessionId, MatchingLastError& type)
{
    std::lock_guard<std::mutex> sessionsLock(_sessionsMutex);
    auto session = _sessions.find(sessionId);
    if(session == _sessions.end())
        return false;

    return _matching->AddWaitSession(sessionId, session->second, type);
}
