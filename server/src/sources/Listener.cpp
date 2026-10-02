#include "Listener.hpp"
#include "ServerPolicy.hpp"

#include "ExecutionContext.hpp"
#include "Room.hpp"
#include "Session.hpp"
#include "PacketPool.hpp"

#include <future>
#include "RuntimeMetrics.hpp"
#ifdef _WIN32
#include <mstcpip.h>
#endif

Listener::Listener(SecretKey, std::shared_ptr<ExecutionContext> ioManager, std::uint16_t tcpPort, std::uint16_t udpPort,
                   const std::vector<std::string> allowedPlayers)
    : _ioManager(ioManager), _strand(ioManager->GetIoContext()), _tcpEndpoint(asio::ip::tcp::v4(), tcpPort),
      _acceptor(ioManager->GetIoContext(), _tcpEndpoint),
      _udpSocket(ioManager->GetIoContext(), asio::ip::udp::endpoint(asio::ip::udp::v4(), udpPort)),
      _allowedPlayers(allowedPlayers.begin(), allowedPlayers.end())
{
    _udpEndpoint = _udpSocket.local_endpoint();
#ifdef _WIN32
    BOOL bNewBehavior = FALSE;
    DWORD dwBytesReturned = 0;
    WSAIoctl(_udpSocket.native_handle(), SIO_UDP_CONNRESET, &bNewBehavior, sizeof(bNewBehavior), nullptr, 0,
             &dwBytesReturned, nullptr, nullptr);
#endif
    spdlog::info("listener object created: tcp port {}, udp port {}", _tcpEndpoint.port(), _udpEndpoint.port());
}

void Listener::Start()
{
    spdlog::info("listener started...");

    asio::post(_strand, [self = GetShared<Listener>()] {
        self->AcceptAsync();
        self->ReceiveAsyncByUdp();
    });
}

void Listener::SetDedicatedRoom(std::shared_ptr<Room> room)
{
    std::lock_guard lock(_dedicatedRoomMutex);
    _dedicatedRoom = std::move(room);
}

std::shared_ptr<Room> Listener::GetDedicatedRoom() const
{
    std::lock_guard lock(_dedicatedRoomMutex);
    return _dedicatedRoom;
}

void Listener::StopInput()
{
    // 같은 strand에서 post 후 기다리면 자기 작업을 실행할 수 없다.
    if (_strand.running_in_this_thread())
    {
        _accepting = false;
        std::error_code ec;
        _acceptor.close(ec);
        return;
    }

    auto done = std::make_shared<std::promise<void>>();
    auto future = done->get_future();
    asio::post(_strand, [self = GetShared<Listener>(), done] {
        self->_accepting = false;
        std::error_code ec;
        self->_acceptor.close(ec);
        done->set_value();
    });
    future.get();
}

bool Listener::IsDrained()
{
    // 반환값은 조회 handler 실행 시점의 snapshot이다. drain 대기 자체가 아니다.
    if (_strand.running_in_this_thread())
        return _pendingOutbound == 0 && _pendingTcp == 0 && _networkBufferQueue.empty() && _udpInFlight == 0;

    auto done = std::make_shared<std::promise<bool>>();
    auto future = done->get_future();
    asio::post(_strand, [self = GetShared<Listener>(), done] {
        done->set_value(self->_pendingOutbound == 0 && self->_pendingTcp == 0 && self->_networkBufferQueue.empty() &&
                        self->_udpInFlight == 0);
    });
    return future.get();
}

void Listener::Stop()
{
    auto done = std::make_shared<std::promise<void>>();
    auto future = done->get_future();
    auto close = [self = GetShared<Listener>(), done] {
        self->_accepting = false;
        self->_closing = true;
        std::error_code ec;
        self->_acceptor.close(ec);
        self->_udpSocket.close(ec);
        std::vector<std::shared_ptr<Session>> sessions;
        {
            std::lock_guard lock(self->_sessionsMutex);
            for (auto &[id, session] : self->_sessions)
                sessions.push_back(session);
            for (auto &session : self->_pendingSessions)
                sessions.push_back(session);
        }
        for (auto &session : sessions)
            session->Stop();
        done->set_value();
    };
    if (_strand.running_in_this_thread())
        close();
    else
        asio::post(_strand, std::move(close));
    future.get();
}

void Listener::ConnectionReady(const std::shared_ptr<Session> &session, asio::ip::udp::endpoint ep)
{
    asio::post(_strand, [self = GetShared<Listener>(), session, ep] {
        if (!self->_accepting || session->IsStopping() || !session->IsValid())
            return;
        const auto id = session->GetId();
        auto found = self->_connections.find(id);
        if (found != self->_connections.end())
            return;
        Connection connection{{id, session->GetGeneration()}, ep, session};
        self->_connections.emplace(id, connection);
        if (auto room = self->GetDedicatedRoom())
            room->AddSession(id, {connection.recipient.generation, session->GetPresetId()});
    });
}

void Listener::PostSend(OutboundMessage message)
{
    bool schedule = false;
    {
        std::lock_guard lock(_outboundMutex);
        if (message.stateKey.has_value())
        {
            for (auto &queued : _outboundQueue)
            {
                if (queued.stateKey == message.stateKey && queued.transport == message.transport)
                {
                    queued = std::move(message);
                    return;
                }
            }
        }
        if (_outboundQueue.size() >= ServerPolicy::PendingOutbound)
        {
            auto stale = std::find_if(_outboundQueue.begin(), _outboundQueue.end(),
                                      [](const OutboundMessage &queued) {
                                          return queued.stateKey.has_value();
                                      });
            if (stale == _outboundQueue.end())
                return;
            _outboundQueue.erase(stale);
            --_pendingOutbound;
        }
        _outboundQueue.push_back(std::move(message));
        ++_pendingOutbound;
        if (!_outboundScheduled)
        {
            _outboundScheduled = true;
            schedule = true;
        }
    }
    if (schedule)
        asio::post(_strand, [self = GetShared<Listener>()] {
            self->DrainOutbound();
        });
}

void Listener::DrainOutbound()
{
    // Yield between small batches so socket completions/input are not starved
    // by a burst of game-produced broadcast requests.
    for (int count = 0; count < ServerPolicy::OutboundBatch; ++count)
    {
        OutboundMessage message;
        {
            std::lock_guard lock(_outboundMutex);
            if (_outboundQueue.empty())
            {
                _outboundScheduled = false;
                return;
            }
            message = std::move(_outboundQueue.front());
            _outboundQueue.pop_front();
            --_pendingOutbound;
        }
        SendOnStrand(message);
    }
    asio::post(_strand, [self = GetShared<Listener>()] {
        self->DrainOutbound();
    });
}

void Listener::SendOnStrand(const OutboundMessage &message)
{
    if (_closing)
        return;
    RuntimeMetrics::Observe("game_to_network", std::chrono::steady_clock::now() - message.submitted,
                            _pendingOutbound.load());
    const auto serializeStart = std::chrono::steady_clock::now();
    NetworkPacket packet;
    packet.set_type(message.type);
    packet.set_data(*message.payload);
    const auto size = packet.ByteSizeLong();
    if (size > NetworkFraming::MaximumUdpPayloadBytes)
        return;
    auto sendBuffer = std::make_shared<Raw>(size + NetworkFraming::LengthHeaderBytes);
    const auto netSize = htons(static_cast<std::uint16_t>(size));
    std::memcpy(sendBuffer->data(), &netSize, NetworkFraming::LengthHeaderBytes);
    if (!packet.SerializeToArray(sendBuffer->data() + NetworkFraming::LengthHeaderBytes, static_cast<int>(size)))
        return;
    RuntimeMetrics::Observe("network_serialization", std::chrono::steady_clock::now() - serializeStart);
    for (const auto &recipient : message.recipients)
    {
        if (message.transport == Transport::Tcp)
        {
            std::shared_ptr<Session> session;
            {
                std::lock_guard lock(_sessionsMutex);
                auto found = _sessions.find(recipient.id);
                if (found != _sessions.end() && found->second->GetGeneration() == recipient.generation)
                    session = found->second;
            }
            if (session)
            {
                _pendingTcp.fetch_add(1);
                session->EnqueueTcpBuffer(sendBuffer, [weak = GetWeak<Listener>()] {
                    if (auto owner = weak.lock())
                        owner->_pendingTcp.fetch_sub(1);
                });
            }
            continue;
        }
        auto it = _connections.find(recipient.id);
        if (it == _connections.end() || it->second.recipient.generation != recipient.generation)
            continue;
        EnqueueSendDataOnStrand(it->second.endpoint, sendBuffer, message.stateKey, message.submitted);
    }
}

void Listener::AcceptAsync()
{
    if (!_accepting)
        return;
    auto newSession = Session::Create(_ioManager, GetWeak<Listener>());

    newSession->AddDisconnectCallback([weakSelf = GetWeak<Listener>()](const std::shared_ptr<Session> &session) {
        if (const auto self = weakSelf.lock())
        {
            asio::post(self->_strand, [self, session] {
                {
                    std::lock_guard lock(self->_sessionsMutex);
                    self->_pendingSessions.erase(session);
                    auto it = self->_sessions.find(session->GetId());
                    if (it != self->_sessions.end() && it->second == session)
                        self->_sessions.erase(it);
                }
                auto it = self->_connections.find(session->GetId());
                if (it != self->_connections.end() && it->second.session.lock() == session)
                {
                    if (auto room = self->GetDedicatedRoom())
                        room->RemoveSession(it->first, it->second.recipient.generation);
                    self->_connections.erase(it);
                }
            });
        }
    });

    // Udp Send handler register
    newSession->SetSendToHandler(
        [weakSelf = GetWeak<Listener>()](asio::ip::udp::endpoint ep, std::shared_ptr<const Raw> networkBuffer) {
            if (const auto self = weakSelf.lock())
                self->EnqueueSendData(ep, std::move(networkBuffer));
        });

    {
        std::lock_guard<std::mutex> sessionsLock(_sessionsMutex);
        _pendingSessions.insert(newSession);
    }

    // async accept new client
    _acceptor.async_accept(
        *newSession->GetSocket(),
        asio::bind_executor(_strand, [weakSelf = GetWeak<Listener>(), newSession](const std::error_code &ec) {
            if (ec)
            {
                if (const auto self = weakSelf.lock())
                {
                    std::lock_guard<std::mutex> sessionsLock(self->_sessionsMutex);
                    self->_pendingSessions.erase(newSession);
                }

                if (ec == asio::error::connection_aborted || ec == asio::error::operation_aborted)
                {
                    spdlog::info("listener: acceptor aborted");
                    return;
                }

                spdlog::error("listener: accept error occured({})", ec.message());
                return;
            }

            if (auto self = weakSelf.lock())
            {
                bool reject = false;
                {
                    std::lock_guard lock(self->_sessionsMutex);
                    reject = self->_pendingSessions.size() > ServerPolicy::PendingConnections;
                    if (reject)
                        self->_pendingSessions.erase(newSession);
                }
                if (reject || !self->_accepting)
                    newSession->Stop();
                else
                    newSession->Init();

                // new session create for accept other client
                self->AcceptAsync();
            }
        }));
}

bool Listener::RegisterAuthenticatedSession(const std::string &userId, const std::shared_ptr<Session> &session)
{
    if (!session)
        return false;

    const auto userUuid = uuids::uuid::from_string(userId);
    if (!userUuid.has_value())
    {
        spdlog::error("listener: invalid UUID format in InfoHandshake userId '{}'", userId);
        return false;
    }

    const std::string normalizedId = uuids::to_string(userUuid.value());
    if (!_allowedPlayers.empty() && !IsPlayerAllowed(userId) && !IsPlayerAllowed(normalizedId))
    {
        spdlog::warn("listener: unauthorized userId '{}' rejected (not in allowedPlayers)", userId);
        return false;
    }

    {
        std::lock_guard<std::mutex> sessionsLock(_sessionsMutex);
        if (_sessions.contains(userUuid.value()) || session->GetId() != uuids::uuid{})
            return false;
        _pendingSessions.erase(session);
        session->SetId(userUuid.value());
        _sessions[userUuid.value()] = session;
    }

    spdlog::info("listener: session authenticated and bound to userId {}", normalizedId);
    return true;
}

void Listener::EnqueueSendData(asio::ip::udp::endpoint ep, std::shared_ptr<const Raw> networkBuffer)
{
    asio::post(_strand, [weakSelf = GetWeak<Listener>(), ep, networkBuffer = std::move(networkBuffer)]() mutable {
        if (const auto self = weakSelf.lock())
            self->EnqueueSendDataOnStrand(ep, std::move(networkBuffer));
    });
}

void Listener::EnqueueSendDataOnStrand(asio::ip::udp::endpoint ep, std::shared_ptr<const Raw> networkBuffer,
                                       std::optional<StateKey> stateKey,
                                       std::chrono::steady_clock::time_point submitted)
{
    if (_closing)
        return;
    if (stateKey.has_value())
    {
        for (auto &queued : _networkBufferQueue)
        {
            if (queued.endpoint == ep && queued.stateKey == stateKey)
            {
                queued.sendBuffer = std::move(networkBuffer);
                queued.submitted = submitted;
                return;
            }
        }
    }
    if (_networkBufferQueue.size() >= ServerPolicy::UdpSends)
    {
        auto stale = std::ranges::find_if(_networkBufferQueue,
                                          [](const UdpSend &send) {
                                              return send.stateKey.has_value();
                                          });
        if (stale == _networkBufferQueue.end())
            return;
        _networkBufferQueue.erase(stale);
    }
    _networkBufferQueue.push_back({ep, std::move(networkBuffer), std::move(stateKey), submitted});
    SendAsyncByUdp();
}

void Listener::SendAsyncByUdp()
{
    // One full recipient fan-out may be in flight. All initiations and completions
    // still execute on this strand; datagram buffers are immutable and shared.
    const auto window = std::max<std::size_t>(ServerPolicy::MinimumUdpInFlight, _allowedPlayers.size());
    while (!_closing && !_networkBufferQueue.empty() && _udpInFlight < window)
    {
        auto send = std::move(_networkBufferQueue.front());
        _networkBufferQueue.pop_front();
        auto ep = send.endpoint;
        auto networkBuffer = std::move(send.sendBuffer);
        RuntimeMetrics::Observe("game_to_udp_start", std::chrono::steady_clock::now() - send.submitted,
                                _networkBufferQueue.size());
        ++_udpInFlight;
        const auto sendBuffer = asio::buffer(*networkBuffer);
        _udpSocket.async_send_to(sendBuffer, ep,
                                 asio::bind_executor(_strand, [weakSelf = GetWeak<Listener>(),
                                                         networkBuffer](const std::error_code &ec, std::size_t) {
                                                         if (const auto self = weakSelf.lock())
                                                         {
                                                             --self->_udpInFlight;
                                                             if (ec && ec != asio::error::operation_aborted)
                                                                 spdlog::warn("UDP send failed: {}", ec.message());
                                                             self->SendAsyncByUdp();
                                                         }
                                                     }));
    }
}

void Listener::ReceiveAsyncByUdp()
{
    auto receiveBuffer = ByteBufferPool::GetInstance()->Rent();
    if (receiveBuffer->size() < NetworkFraming::UdpReceiveBufferBytes)
        receiveBuffer->resize(NetworkFraming::UdpReceiveBufferBytes);

    auto senderEndpoint = std::make_shared<asio::ip::udp::endpoint>();
    _udpSocket.async_receive_from(
        asio::buffer(*receiveBuffer), *senderEndpoint,
        asio::bind_executor(_strand, [weakSelf = GetWeak<Listener>(), receiveBuffer,
                                senderEndpoint](const std::error_code &ec, const std::size_t bytesRead) {
                                if (ec)
                                {
                                    if (ec == asio::error::operation_aborted)
                                    {
                                        spdlog::info("listener: udp socket close complete");
                                        return;
                                    }

                                    if (ec != asio::error::connection_reset && ec != asio::error::connection_refused)
                                    {
                                        spdlog::warn("listener: udp error occurred({})", ec.message());
                                    }

                                    if (auto self = weakSelf.lock())
                                    {
                                        self->ReceiveAsyncByUdp();
                                        return;
                                    }
                                }

                                if (bytesRead < sizeof(std::uint16_t))
                                {
                                    spdlog::error("listener: bad size received (size {})", bytesRead);
                                    if (const auto self = weakSelf.lock())
                                        self->ReceiveAsyncByUdp();

                                    return;
                                }

                                // first 2 bytes are data length header
                                std::uint16_t expectedSize;
                                std::memcpy(&expectedSize, receiveBuffer->data(), sizeof(expectedSize));
                                expectedSize = ntohs(expectedSize);

                                std::size_t payloadSize = bytesRead - sizeof(std::uint16_t);

                                if (expectedSize != payloadSize)
                                {
                                    spdlog::error("listener: bad data received (expected {}, real {})", expectedSize,
                                                  payloadSize);
                                    if (const auto self = weakSelf.lock())
                                        self->ReceiveAsyncByUdp();

                                    return;
                                }

                                // raw pointer to data payload, no onwership
                                const unsigned char *payload =
                                    receiveBuffer->data() + NetworkFraming::LengthHeaderBytes;

                                // send to room
                                // client must have own room id and session id
                                if (const auto self = weakSelf.lock())
                                {
                                    if (self->_accepting)
                                        self->ProcessPacket(senderEndpoint, static_cast<std::uint16_t>(payloadSize),
                                                            payload);
                                }

                                if (const auto self = weakSelf.lock())
                                    self->ReceiveAsyncByUdp();
                            }));
}

void Listener::ProcessPacket(std::shared_ptr<asio::ip::udp::endpoint> sender, std::uint16_t size,
                             const unsigned char *data)
{
    const auto now = std::chrono::steady_clock::now();
    if (now - _receiveWindow >= ServerPolicy::RateWindow)
    {
        _receiveWindow = now;
        _receiveCount = 0;
    }
    if (++_receiveCount > ServerPolicy::GlobalUdpRate || size > ServerPolicy::MaximumInboundBytes)
        return;
    NetworkPacket packet;
    if (!packet.ParseFromArray(data, size))
    {
        spdlog::error("listener: parsing udp real data error");
        return;
    }

    if (packet.type() == PacketType::Ingame)
    {
        // Decode only the outer envelope here. Game parses IngamePacket on its executor.
        // Wire session ID is validated on Game against this authenticated endpoint identity.
        for (auto &[id, connection] : _connections)
        {
            if (connection.endpoint != *sender)
                continue;
            auto now = std::chrono::steady_clock::now();
            if (now - connection.window >= ServerPolicy::RateWindow)
            {
                connection.window = now;
                connection.inputCount = 0;
            }
            if (++connection.inputCount > ServerPolicy::ConnectionInputRate)
                return;
            if (auto room = GetDedicatedRoom())
                room->PostInput(packet.data(), connection.recipient);
            break;
        }
        return;
    }

    // Client UDP HolePunching
    if (packet.type() == PacketType::Authentication)
    {
        AuthenticationPacket authPacket;
        if (!authPacket.ParseFromString(packet.data()))
        {
            spdlog::error("listener: parsing authentication packet error");
            return;
        }

        const auto sessionId = uuids::uuid::from_string(authPacket.sessionid());
        if (!sessionId.has_value())
        {
            spdlog::error("listener: auth packet has no session id");
            return;
        }

        // move to matching, wait for matchmaking
        {
            std::lock_guard<std::mutex> sessionsLock(_sessionsMutex);
            if (!_sessions.contains(sessionId.value()))
            {
                spdlog::error("listener: no session in listener");
                return;
            }

            _sessions[sessionId.value()]->PunchUdpHole(*sender);
        }
    }
}