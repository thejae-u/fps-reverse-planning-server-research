#include "Listener.hpp"

#include "IOManager.hpp"
#include "Room.hpp"
#include "Session.hpp"
#include "IngamePacketPool.hpp"

Listener::Listener(SecretKey, std::shared_ptr<IOManager> ioManager, std::uint16_t tcpPort, std::uint16_t udpPort)
    : _ioManager(ioManager), _strand(ioManager->GetIoContext()), _tcpEndpoint(asio::ip::tcp::v4(), tcpPort),
      _acceptor(ioManager->GetIoContext(), _tcpEndpoint), _udpSocket(ioManager->GetIoContext(), asio::ip::udp::endpoint(asio::ip::udp::v4(), udpPort))
{
    _udpEndpoint = _udpSocket.local_endpoint();
    spdlog::info("listener object created: tcp port {}, udp port {}", _tcpEndpoint.port(), _udpEndpoint.port());
}

void Listener::Start()
{
    spdlog::info("listener started...");

    AcceptAsync();
    ReceiveAsyncByUdp();
}

void Listener::Stop()
{
    if(_dedicatedRoom)
    {
        _dedicatedRoom->Stop();
        _dedicatedRoom = nullptr;
    }

    _acceptor.close();
    _udpSocket.close();
    spdlog::info("listener stopped...\n");
}

void Listener::AcceptAsync()
{
    uuids::uuid sessionId;
    {
        std::lock_guard<std::mutex> lock(_uuidMutex);
        sessionId = _uuidGen(); // TODO : 인증 서버로부터 받은 player uuid를 사용할것인지 고민 -> 데이터 저장을 위한 조치 혹은 플레이어 아이디의 관리
    }

    auto newSession = Session::Create(_ioManager, GetWeak<Listener>(), sessionId, _udpEndpoint.port());
    {
        std::lock_guard<std::mutex> sessionsLock(_sessionsMutex);
        if(_sessions.contains(sessionId))
        {
            spdlog::error("listener: create duplicate session, create again");
            AcceptAsync();
            return;
        }
    }

    newSession->AddDisconnectCallback([weakSelf = GetWeak<Listener>(), sessionId](const std::shared_ptr<Session>& session) {
        if(const auto self = weakSelf.lock())
        {
            std::lock_guard<std::mutex> sessionsLock(self->_sessionsMutex);
            self->_sessions.erase(sessionId);
            spdlog::info("listener: session {} removed from listener", uuids::to_string(sessionId));
        }
    });

    // Udp Send handler register
    newSession->SetSendToHandler([weakSelf = GetWeak<Listener>()](asio::ip::udp::endpoint ep, std::shared_ptr<Raw> data) {
        if(const auto self = weakSelf.lock())
            self->EnqueueSendData(ep, std::move(data));
    });

    _sessions[sessionId] = newSession;

    // async accept new client
    _acceptor.async_accept(*newSession->GetSocket(), [weakSelf = GetWeak<Listener>(), newSession, sessionId](const std::error_code& ec) {
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
            newSession->Init();

            // new session create for accept other client
            self->AcceptAsync();
        }
    });
}

void Listener::EnqueueSendData(asio::ip::udp::endpoint ep, const std::shared_ptr<Raw> payload)
{
    {
        std::lock_guard<std::mutex> payloadQueueLock(_payloadQueueMutex);
        _payloadQueue.push({ ep, payload });
    }

    if(_isSending.exchange(true))
        return;

    SendAsyncByUdp();
}

void Listener::SendAsyncByUdp()
{
    {
        std::lock_guard<std::mutex> payloadQueueLock(_payloadQueueMutex);
        if(_noLockPayloadQueue.empty())
            std::swap(_payloadQueue, _noLockPayloadQueue);
    }

    if(_noLockPayloadQueue.empty()) // payloadQueue도 없음
    {
        _isSending = false;
        return;
    }

    auto [ep, payload] = std::move(_noLockPayloadQueue.front());
    _noLockPayloadQueue.pop();
    _isSending = true;

    // 실행 후 바로 return -> thread 점유 최소화
    _udpSocket.async_send_to(
        asio::buffer(*payload), ep,
        asio::bind_executor(_strand, [weakSelf = GetWeak<Listener>(), payload, ep](const std::error_code& ec, std::size_t) {
            if(const auto self = weakSelf.lock())
            {
                // TODO : error code에 따라 작동 여부 결정
                if(ec)
                    spdlog::error("listener: udp send error occured({})", ec.message());

                self->SendAsyncByUdp(); // 다시 실행하여 나머지 전송
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
                if(const auto self = weakSelf.lock())
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
                if(const auto self = weakSelf.lock())
                    self->ReceiveAsyncByUdp();

                return;
            }

            // raw pointer to data payload, no onwership
            const unsigned char* payload = receiveBuffer->data() + 2;

            // send to room
            // client must have own room id and session id
            if(const auto self = weakSelf.lock())
            {
                self->_ioManager->PostOnBlockingPool([weakSelf, senderEndpoint, receiveBuffer, payload, payloadSize]() {
                    if(const auto self = weakSelf.lock())
                        self->ProcessPacket(std::move(senderEndpoint), payloadSize, payload);
                });
            }

            if(const auto self = weakSelf.lock())
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
        auto ingamePacket = IngamePacketPool::GetInstance()->Rent();
        if(!ingamePacket->ParseFromString(packet.data()))
        {
            spdlog::error("listener: parsing ingame packet error");
            return;
        }

        if(!_dedicatedRoom)
        {
            spdlog::warn("listener: ingame packet arrived but dedicated room is null");
            return;
        }

        _dedicatedRoom->EnqueuePacket(std::move(ingamePacket));
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

        const auto sessionId = uuids::uuid::from_string(authPacket.sessionid());
        if(!sessionId.has_value())
        {
            spdlog::error("listener: auth packet has no session id");
            return;
        }

        // move to matching, wait for matchmaking
        {
            std::lock_guard<std::mutex> sessionsLock(_sessionsMutex);
            if(!_sessions.contains(sessionId.value()))
            {
                spdlog::error("listener: no session in listener");
                return;
            }

            _sessions[sessionId.value()]->PunchUdpHole(*sender);
        }
    }
}