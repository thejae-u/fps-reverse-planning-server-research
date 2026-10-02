#include "Session.hpp"
#include "ServerPolicy.hpp"

#include "PacketPool.hpp"
#include "asio.hpp"
#include "Listener.hpp"
#include "CustomUtility.hpp"
#include "Room.hpp"
#include "Player.hpp"
#include "RuntimeMetrics.hpp"

void Session::StartTcpRead()
{
    // Tcp read open
    ReadSizeAsync();
}

void Session::Start()
{
    // no implement
}

void Session::Stop()
{
    // 원자적으로 상태를 변경하여 중복 진입 방지
    if (_stopping.exchange(true))
        return;

    asio::post(_strand, [weakSelf = GetWeak<Session>()]() {
        if (const auto self = weakSelf.lock())
        {
            std::error_code ignored;
            self->_isValid = false;
            self->_state = SessionState::Invalid;
            self->_handshakeTimer.cancel();
            self->_heartbeatTimer.cancel();
            self->_heartbeatPayload.clear();
            self->_socketPtr->close(ignored);

            std::vector<NotifyDisconnectCallback> callbacks;
            {
                std::lock_guard lock(self->_disconnectCallbacksMutex);
                callbacks = self->_disconnectCallbacks;
            }
            for (const auto &callback : callbacks)
                callback(self);
        }
    });
}

void Session::Init()
{
    spdlog::info("session{}: Init (waiting for client InfoHandshake)", uuids::to_string(GetId()));
    asio::post(_strand, [weakSelf = GetWeak<Session>()]() {
        if (auto self = weakSelf.lock())
        {
            if (!self->_stopping && self->_state.load() == SessionState::Initializing)
            {
                self->_handshakeTimer.expires_after(ServerPolicy::HandshakeTimeout);
                self->_handshakeTimer.async_wait(asio::bind_executor(self->_strand, [self](const std::error_code &ec) {
                    if (!ec && !self->_isValid)
                        self->Stop();
                }));
                self->StartTcpRead();
            }
        }
    });
}

void Session::PunchUdpHole(const asio::ip::udp::endpoint &ep)
{
    // IO Context thread로 로직 수행 (non-blocking)
    asio::post(_strand, [weakSelf = GetWeak<Session>(), ep]() {
        if (const auto self = weakSelf.lock())
        {
            if (const auto state = self->_state.load();
                !self->_stopping && (state == SessionState::Initializing || state == SessionState::InitializeComplete))
            {
                if (self->_clientUdpEp.port() == 0)
                {
                    self->_clientUdpEp = ep;
                    self->_state = SessionState::InitializeComplete;
                    self->_isValid = true; // allow broadcast
                    self->_handshakeTimer.cancel();

                    if (const auto listener = self->_weakListener.lock())
                    {
                        if (const auto room = listener->GetDedicatedRoom())
                        {
                            listener->ConnectionReady(self, ep);
                        }
                    }

                    spdlog::info("session {}: udp hole punched successfully. ip: {}, port: {}",
                                 uuids::to_string(self->GetId()), ep.address().to_string(), ep.port());
                }

                // 포트 등록이 성공하여도 클라이언트가 PunchUdpHole을 요청하면 ACK 유실로 판단 -> 재전송
                const auto ackPacket = NetworkPacketPool::GetInstance()->Rent();
                ackPacket->set_type(PacketType::Authentication);

                AuthenticationPacket authResult;
                authResult.set_method(AuthenticationType::AuthenticationOk);
                authResult.set_sessionid(uuids::to_string(self->GetId()));

                std::string serialized;
                authResult.SerializeToString(&serialized);
                ackPacket->set_data(serialized);

                self->EnqueueUdpSendPacket(ackPacket);
            }
        }
    });
}

void Session::ProcessEndGame()
{
    if (!_isValid)
        return;

    auto endPacket = NetworkPacketPool::GetInstance()->Rent();
    endPacket->set_type(PacketType::EndGame);
    EnqueueTcpSendPacket(endPacket);
    EnqueueUdpSendPacket(endPacket);
    spdlog::info("session {}: sent EndGame packet to client", uuids::to_string(GetId()));
}

void Session::AddDisconnectCallback(NotifyDisconnectCallback callback)
{
    std::lock_guard lock(_disconnectCallbacksMutex);
    _disconnectCallbacks.push_back(std::move(callback));
}

void Session::SetSendToHandler(SendToHandler handler)
{
    _sendTo = std::move(handler);
}

void Session::LogTcpReadFailure(const char *stage, const std::error_code &ec, std::size_t transferred,
                                std::size_t expected) const
{
    const bool stopping = _stopping.load();
    const bool disconnected = ec == asio::error::connection_aborted || ec == asio::error::operation_aborted ||
                              ec == asio::error::eof || ec == asio::error::connection_reset;
    const auto level = stopping || disconnected ? spdlog::level::info : spdlog::level::err;
    spdlog::log(level, "session {}: TCP {} read {} (code={}, category={}, message='{}', bytes={}/{})",
                uuids::to_string(GetId()), stage,
                stopping       ? "finished during shutdown"
                : disconnected ? "disconnected"
                               : "failed",
                ec.value(), ec.category().name(), ec.message(), transferred, expected);
}

void Session::ReadSizeAsync()
{
    asio::async_read(
        *_socketPtr, asio::buffer(&_readNetSize, sizeof(_readNetSize)),
        asio::bind_executor(_strand, [self = GetShared<Session>()](const std::error_code &ec, std::size_t transferred) {
            if (ec)
            {
                self->LogTcpReadFailure("size", ec, transferred, sizeof(self->_readNetSize));
                self->Stop();
                return;
            }

            const std::uint16_t dataSize = ntohs(self->_readNetSize);
            if (dataSize == 0 || dataSize > ServerPolicy::MaximumInboundBytes || self->_stopping)
            {
                self->Stop();
                return;
            }
            self->ReadDataAsync(dataSize);
        }));
}

void Session::ReadDataAsync(const std::uint16_t &dataSize)
{
    auto receiveBuffer = std::make_shared<std::vector<unsigned char>>(dataSize);
    asio::async_read(*_socketPtr, asio::buffer(*receiveBuffer),
                     asio::bind_executor(_strand, [self = GetShared<Session>(), receiveBuffer,
                                                   dataSize](const std::error_code &ec, std::size_t transferred) {
                         if (ec)
                         {
                             self->LogTcpReadFailure("data", ec, transferred, dataSize);
                             self->Stop();
                             return;
                         }

                         // 정상 완료가 종료 요청과 겹쳐도 새 처리/읽기를 시작하지 않는다.
                         if (self->_stopping)
                             return;

                         self->EnqueueProcessPacket(receiveBuffer, dataSize);
                         self->ReadSizeAsync();
                     }));
}

void Session::EnqueueProcessPacket(const std::shared_ptr<const Raw> &data, const std::uint16_t size)
{
    auto packet = NetworkPacketPool::GetInstance()->Rent();
    if (!packet->ParseFromArray(data->data(), size))
    {
        spdlog::error("session {}: parsing process packet error", uuids::to_string(GetId()));
        return;
    }

    std::lock_guard<std::mutex> processQueueLock(_processQueueMutex);
    if (_processQueue.size() >= ServerPolicy::TcpProcessing)
    {
        Stop();
        return;
    }
    _processQueue.push(packet);

    if (_isProcessing.exchange(true))
        return;

    asio::post(_strand, [self = GetShared<Session>()] { self->ProcessPacketAsync(); });
}

void Session::ProcessPacketAsync()
{
    while (true)
    {
        std::shared_ptr<NetworkPacket> packet;
        {
            std::lock_guard<std::mutex> processQueueLock(_processQueueMutex);
            if (_processQueue.empty())
            {
                _isProcessing = false;
                return;
            }
            packet = std::move(_processQueue.front());
            _processQueue.pop();
        }

        if (!packet || _stopping)
            continue;

        if (packet->type() == PacketType::Ping)
        {
            HandlePong(*packet);
            continue;
        }

        if (packet->type() == PacketType::InfoHandshake)
        {
            InfoHandshakePacket req;
            if (!req.ParseFromString(packet->data()) || req.sessionid().empty())
            {
                spdlog::error("session {}: invalid InfoHandshake request payload", uuids::to_string(GetId()));
                Stop();
                return;
            }

            const std::string requestedUserId = req.sessionid();
            const auto listener = _weakListener.lock();
            if (!listener || !listener->RegisterAuthenticatedSession(requestedUserId, GetShared<Session>()))
            {
                spdlog::warn("session {}: failed to authenticate userId '{}'. closing session.",
                             uuids::to_string(GetId()), requestedUserId);
                Stop();
                return;
            }

            _presetId.store(req.presetid());
            ScheduleHeartbeat();
            spdlog::info("session {}: InfoHandshake authenticated (userId={}, presetId={})", uuids::to_string(GetId()),
                         uuids::to_string(GetId()), req.presetid());

            if (auto room = listener->GetDedicatedRoom())
                room->PostHandshake({GetId(), GetGeneration()}, GetPresetId());
        }
    }
}

void Session::EnqueueUdpSendPacket(const std::shared_ptr<NetworkPacket> &data)
{
    asio::post(_strand, [weakSelf = GetWeak<Session>(), data]() {
        if (const auto self = weakSelf.lock())
            self->EnqueueUdpSendPacketOnStrand(data);
    });
}

void Session::EnqueueUdpSendPacketOnStrand(std::shared_ptr<NetworkPacket> data)
{
    if (_stopping || _clientUdpEp.port() == 0)
        return;

    // serialize NetworkPacket into payload (body)
    const auto payloadSize = data->ByteSizeLong();
    if (payloadSize > NetworkFraming::MaximumUdpPayloadBytes)
        return;
    const auto size = static_cast<int>(payloadSize);
    const auto payload = std::make_shared<Raw>(size);
    if (!data->SerializeToArray(payload->data(), size))
    {
        spdlog::error("session {}: failed serialize send packet", uuids::to_string(GetId()));
        return;
    }

    const std::uint16_t sendNetSize = static_cast<std::uint16_t>(htons(size));
    // combine 2-byte header + payload into networkBuffer (sendBuffer)
    const auto networkBuffer = std::make_shared<Raw>(sizeof(sendNetSize) + payload->size());
    std::memcpy(networkBuffer->data(), &sendNetSize, sizeof(sendNetSize));
    std::memcpy(networkBuffer->data() + sizeof(sendNetSize), payload->data(), payload->size());

    if (_sendTo == nullptr)
    {
        spdlog::error("session {}: send handler is not set", uuids::to_string(GetId()));
        Stop();
        return;
    }

    // Udp Send
    _sendTo(_clientUdpEp, networkBuffer);
}

void Session::EnqueueTcpSendPacket(const std::shared_ptr<NetworkPacket> &data)
{
    asio::post(_strand, [weakSelf = GetWeak<Session>(), data]() {
        if (const auto self = weakSelf.lock())
            self->EnqueueTcpSendPacketOnStrand(data);
    });
}

void Session::EnqueueTcpSendPacketOnStrand(std::shared_ptr<NetworkPacket> data)
{
    const auto size = data->ByteSizeLong();
    if (size > NetworkFraming::MaximumTcpPayloadBytes)
    {
        Stop();
        return;
    }
    auto sendBuffer = std::make_shared<Raw>(size + NetworkFraming::LengthHeaderBytes);
    const auto netSize = htons(static_cast<std::uint16_t>(size));
    std::memcpy(sendBuffer->data(), &netSize, NetworkFraming::LengthHeaderBytes);
    if (data->SerializeToArray(sendBuffer->data() + NetworkFraming::LengthHeaderBytes, static_cast<int>(size)))
        EnqueueTcpBuffer(sendBuffer);
}

void Session::EnqueueTcpBuffer(std::shared_ptr<const Raw> sendBuffer, std::function<void()> done)
{
    asio::post(_strand,
               [self = GetShared<Session>(), sendBuffer = std::move(sendBuffer), done = std::move(done)]() mutable {
                   if (self->_stopping || self->_sendTcpQueue.size() >= ServerPolicy::TcpSends)
                   {
                       if (done)
                           done();
                       if (!self->_stopping)
                           self->Stop();
                       return;
                   }
                   self->_sendTcpQueue.push({std::move(sendBuffer), std::move(done)});
                   if (self->_isWriting)
                       return;
                   self->_isWriting = true;
                   self->DoSendAsyncTcpLoop();
               });
}

void Session::DoSendAsyncTcpLoop()
{
    if (_sendTcpQueue.empty())
    {
        _isWriting = false;
        return;
    }
    auto send = std::move(_sendTcpQueue.front());
    _sendTcpQueue.pop();
    const auto sendBuffer = send.sendBuffer;
    asio::async_write(*_socketPtr, asio::buffer(*sendBuffer),
                      asio::bind_executor(_strand, [self = GetShared<Session>(), send = std::move(send)](
                                                       const std::error_code &ec, std::size_t transferred) {
                          if (send.done)
                              send.done();
                          if (ec)
                          {
                              spdlog::log(self->_stopping ? spdlog::level::warn : spdlog::level::err,
                                          "session {}: TCP write failed (stopping={}, code={}, category={}, "
                                          "message='{}', bytes={}/{}, discarded_pending={})",
                                          uuids::to_string(self->GetId()), self->_stopping.load(), ec.value(),
                                          ec.category().name(), ec.message(), transferred, send.sendBuffer->size(),
                                          self->_sendTcpQueue.size());
                              self->Stop();
                              while (!self->_sendTcpQueue.empty())
                              {
                                  auto queued = std::move(self->_sendTcpQueue.front());
                                  self->_sendTcpQueue.pop();
                                  if (queued.done)
                                      queued.done();
                              }
                              self->_isWriting = false;
                              return;
                          }
                          self->DoSendAsyncTcpLoop();
                      }));
}

void Session::ScheduleHeartbeat()
{
    if (_stopping)
        return;

    _heartbeatTimer.expires_after(ServerPolicy::HeartbeatInterval);
    _heartbeatTimer.async_wait(asio::bind_executor(_strand, [weakSelf = GetWeak<Session>()](const std::error_code &ec) {
        const auto self = weakSelf.lock();
        if (!self || ec || self->_stopping)
            return;

        const auto now = std::chrono::steady_clock::now();
        if (!self->_heartbeatPayload.empty())
        {
            if (now - self->_heartbeatRequestedAt >= ServerPolicy::HeartbeatTimeout)
            {
                spdlog::warn("session {}: TCP heartbeat timeout (generation={})", uuids::to_string(self->GetId()),
                             self->GetGeneration());
                self->Stop();
                return;
            }
        }
        else
        {
            // 한 번에 하나만 대기한다. 다른/중복/이전 응답은 생존 시간을 연장하지 않는다.
            self->_heartbeatPayload = std::to_string(++self->_heartbeatSequence);
            self->_heartbeatRequestedAt = now;
            auto ping = std::make_shared<NetworkPacket>();
            ping->set_type(PacketType::Ping);
            ping->set_data(self->_heartbeatPayload);
            self->EnqueueTcpSendPacketOnStrand(std::move(ping));
        }

        self->ScheduleHeartbeat();
    }));
}

void Session::HandlePong(const NetworkPacket &packet)
{
    if (_stopping || _heartbeatPayload.empty() || packet.data() != _heartbeatPayload)
        return;

    const auto elapsed = std::chrono::steady_clock::now() - _heartbeatRequestedAt;
    // timer handler보다 먼저 실행된 늦은 pong도 만료된 요청을 되살리지 못한다.
    if (elapsed >= ServerPolicy::HeartbeatTimeout)
        return;

    // Network 큐 대기와 상대 처리 시간을 포함한 관측 RTT이다.
    RuntimeMetrics::Observe("tcp_ping_rtt", elapsed);
    PingRttPacket report;
    report.set_sequence(_heartbeatSequence);
    report.set_microseconds(
        static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(elapsed).count()));
    auto response = std::make_shared<NetworkPacket>();
    response->set_type(PacketType::PingRtt);
    if (report.SerializeToString(response->mutable_data()))
        EnqueueTcpSendPacketOnStrand(std::move(response));
    _heartbeatPayload.clear();
}
