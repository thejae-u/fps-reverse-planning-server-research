#include "Session.hpp"

#include "PacketPool.hpp"
#include "asio.hpp"
#include "Listener.hpp"
#include "CustomUtility.hpp"
#include "Room.hpp"
#include "Player.hpp"

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
    if(_state.exchange(SessionState::Invalid) == SessionState::Invalid)
        return;

    _isValid = false;
    _socketPtr->close();

    _disconnectCallback(GetShared<Session>());
}

void Session::Init()
{
    spdlog::info("session{}: Init (waiting for client InfoHandshake)", uuids::to_string(_id));
    asio::post(_strand, [weakSelf = GetWeak<Session>()]() {
        if(auto self = weakSelf.lock())
        {
            self->_state = SessionState::Initializing;
            self->StartTcpRead();
        }
    });
}

void Session::PunchUdpHole(const asio::ip::udp::endpoint& ep)
{
    // IO Context thread로 로직 수행 (non-blocking)
    asio::post(_strand, [weakSelf = GetWeak<Session>(), ep]() {
        if(const auto self = weakSelf.lock())
        {
            if(const auto state = self->_state.load(); state == SessionState::Initializing)
            {
                if(self->_clientUdpEp.port() == 0)
                {
                    self->_clientUdpEp = ep;
                    self->_state = SessionState::InitializeComplete;
                    self->_isValid = true; // allow broadcast

                    if(const auto listener = self->_weakListener.lock())
                    {
                        if(const auto room = listener->GetDedicatedRoom())
                        {
                            room->AddSession(self->_id, self->GetWeak<Session>());
                        }
                    }

                    spdlog::info("session {}: udp hole punched successfully. ip: {}, port: {}",
                                 uuids::to_string(self->_id), ep.address().to_string(), ep.port());
                }

                // 포트 등록이 성공하여도 클라이언트가 PunchUdpHole을 요청하면 ACK 유실로 판단 -> 재전송
                const auto ackPacket = NetworkPacketPool::GetInstance()->Rent();
                ackPacket->set_type(PacketType::Authentication);

                AuthenticationPacket authResult;
                authResult.set_method(AuthenticationType::AuthenticationOk);
                authResult.set_sessionid(uuids::to_string(self->_id));

                std::string serialized;
                authResult.SerializeToString(&serialized);
                ackPacket->set_data(serialized);

                self->EnqueueUdpSendPacket(ackPacket);
            }
        }
    });
}

void Session::AddDisconnectCallback(NotifyDisconnectCallback callback)
{
    _disconnectCallback = std::move(callback);
}

void Session::SetSendToHandler(SendToHandler handler)
{
    _sendTo = std::move(handler);
}

void Session::ReadSizeAsync()
{
    asio::async_read(*_socketPtr, asio::buffer(&_readNetSize, sizeof(_readNetSize)), asio::bind_executor(_strand, [weakSelf = GetWeak<Session>()](const std::error_code& ec, std::size_t) {
        if(ec)
        {
            if(const auto self = weakSelf.lock())
            {
                if(ec == asio::error::connection_aborted || ec == asio::error::operation_aborted || ec == asio::error::eof || ec == asio::error::connection_reset)
                {
                    spdlog::info("session {}: size read aborted... disconnect", uuids::to_string(self->GetId()));
                }
                else
                {
                    spdlog::error("session {}: size read error...({}) disconnect", uuids::to_string(self->GetId()), ec.message());
                }

                self->Stop();
            }

            return;
        }

        if(const auto self = weakSelf.lock())
        {
            std::uint16_t dataSize = ntohs(self->_readNetSize);
            self->ReadDataAsync(dataSize);
        }
    }));
}

void Session::ReadDataAsync(const std::uint16_t& dataSize)
{
    auto receiveBuffer = std::make_shared<std::vector<unsigned char>>(dataSize);
    asio::async_read(*_socketPtr, asio::buffer(*receiveBuffer), asio::bind_executor(_strand, [weakSelf = GetWeak<Session>(), receiveBuffer, dataSize](const std::error_code& ec, std::size_t) {
        if(const auto self = weakSelf.lock())
        {
            if(ec)
            {
                if(ec == asio::error::connection_aborted || ec == asio::error::operation_aborted || ec == asio::error::eof || ec == asio::error::connection_reset)
                {
                    spdlog::info("session {}: data read aborted... disconnect", uuids::to_string(self->GetId()));
                }
                else
                {
                    spdlog::error("session {}: data read error...({}) disconnect", uuids::to_string(self->GetId()), ec.message());
                }
                self->Stop();
                return;
            }

            self->_ioManager->PostOnBlockingPool([weakSelf, receiveBuffer, dataSize]() {
                if(const auto session = weakSelf.lock())
                {
                    session->EnqueueProcessPacket(receiveBuffer, dataSize);
                }
            });

            self->ReadSizeAsync();
        }
    }));
}

void Session::EnqueueProcessPacket(const std::shared_ptr<Raw>& data, const std::uint16_t size)
{
    auto packet = NetworkPacketPool::GetInstance()->Rent();
    if(!packet->ParseFromArray(data->data(), size))
    {
        spdlog::error("session {}: parsing process packet error", uuids::to_string(_id));
        return;
    }

    std::lock_guard<std::mutex> processQueueLock(_processQueueMutex);
    _processQueue.push(packet);

    if(_isProcessing.exchange(true))
        return;

    _ioManager->PostOnBlockingPool([weakSelf = GetWeak<Session>()]() {
        if(const auto self = weakSelf.lock())
            self->ProcessPacketAsync();
    });
}

void Session::ProcessPacketAsync()
{
    while(true)
    {
        std::shared_ptr<NetworkPacket> packet;
        {
            std::lock_guard<std::mutex> processQueueLock(_processQueueMutex);
            if(_processQueue.empty())
            {
                _isProcessing = false;
                return;
            }
            packet = std::move(_processQueue.front());
            _processQueue.pop();
        }

        if(!packet)
            continue;

        if(packet->type() == PacketType::InfoHandshake)
        {
            InfoHandshakePacket req;
            if(!req.ParseFromString(packet->data()) || req.sessionid().empty())
            {
                spdlog::error("session {}: invalid InfoHandshake request payload", uuids::to_string(_id));
                Stop();
                return;
            }

            const std::string requestedUserId = req.sessionid();
            const auto listener = _weakListener.lock();
            if(!listener || !listener->RegisterAuthenticatedSession(requestedUserId, GetShared<Session>()))
            {
                spdlog::warn("session {}: failed to authenticate userId '{}'. closing session.", uuids::to_string(_id), requestedUserId);
                Stop();
                return;
            }

            _presetId.store(req.presetid());
            spdlog::info("session {}: InfoHandshake authenticated (userId={}, presetId={})", uuids::to_string(_id), uuids::to_string(_id), req.presetid());

            InfoHandshakePacket resp;
            resp.set_sessionid(uuids::to_string(_id));
            resp.set_presetid(_presetId.load());
            resp.set_teamid(0);
            resp.set_spawnx(0.0f);
            resp.set_spawny(0.0f);
            resp.set_spawnz(0.0f);
            resp.set_movespeed(BASE_MOVE_SPEED);
            resp.set_sprintspeed(static_cast<float>(MAX_SPEED));
            resp.set_jumpspeed(JUMP_SPEED);
            resp.set_gravity(GRAVITY);
            resp.set_maxhp(100);
            resp.set_attackpower(10);
            resp.set_maxammo(30);

            std::string serializedResp;
            if(resp.SerializeToString(&serializedResp))
            {
                auto sendPacket = NetworkPacketPool::GetInstance()->Rent();
                sendPacket->set_type(PacketType::InfoHandshake);
                sendPacket->set_data(serializedResp);
                EnqueueTcpSendPacket(std::move(sendPacket));
            }
        }
    }
}

void Session::EnqueueUdpSendPacket(const std::shared_ptr<NetworkPacket>& data)
{
    if(_clientUdpEp.port() == 0)
        return;

    // serialize NetworkPacket into payload (body)
    auto size = static_cast<int>(data->ByteSizeLong());
    const auto payload = std::make_shared<Raw>(size);
    if(!data->SerializeToArray(payload->data(), size))
    {
        spdlog::error("session {}: failed serialize send packet", uuids::to_string(GetId()));
        return;
    }

    const std::uint16_t sendNetSize = static_cast<std::uint16_t>(htons(size));
    // combine 2-byte header + payload into networkBuffer (physical wire buffer)
    const auto networkBuffer = std::make_shared<Raw>(sizeof(sendNetSize) + payload->size());
    std::memcpy(networkBuffer->data(), &sendNetSize, sizeof(sendNetSize));
    std::memcpy(networkBuffer->data() + sizeof(sendNetSize), payload->data(), payload->size());

    if(_sendTo == nullptr)
    {
        spdlog::error("session {}: send handler is not set", uuids::to_string(GetId()));
        Stop();
        return;
    }

    // Udp Send
    _sendTo(_clientUdpEp, networkBuffer);
}

void Session::EnqueueTcpSendPacket(const std::shared_ptr<NetworkPacket>& data)
{
    auto size = static_cast<int>(data->ByteSizeLong());
    const auto payload = std::make_shared<Raw>(size);

    if(!data->SerializeToArray(payload->data(), size))
    {
        spdlog::error("session {}: failed to serialize tcp data", uuids::to_string(_id));
        return;
    }

    {
        std::lock_guard<std::mutex> sendQueueLock(_sendTcpQueueMutex);
        _sendTcpQueue.push(payload);
    }

    if(_isWriting.exchange(true))
        return;

    DoSendAsyncTcpLoop();
}

void Session::DoSendAsyncTcpLoop()
{
    // Dequeue payload (body) from send queue
    std::shared_ptr<Raw> payload;
    {
        std::lock_guard<std::mutex> sendQueueLock(_sendTcpQueueMutex);
        if(_sendTcpQueue.empty())
        {
            _isWriting = false;
            return;
        }
        payload = std::move(_sendTcpQueue.front());
        _sendTcpQueue.pop();
    }

    // calculate size for framing
    const std::uint16_t size = payload->size();
    const std::uint16_t netSize = htons(size);
    const auto totalSize = sizeof(size) + size;

    // make networkBuffer (size header + payload)
    auto networkBuffer = std::make_shared<Raw>(totalSize);
    std::memcpy(networkBuffer->data(), &netSize, sizeof(netSize));
    std::memcpy(networkBuffer->data() + sizeof(netSize), payload->data(), size);

    asio::async_write(*_socketPtr, asio::buffer(*networkBuffer), asio::bind_executor(_strand, [weakSelf = GetWeak<Session>(), networkBuffer, totalSize](const std::error_code& ec, std::size_t bytesTransferred) {
        if(const auto self = weakSelf.lock())
        {
            if(ec)
            {
                if(ec != asio::error::operation_aborted)
                    spdlog::error("session {} write error: {}", uuids::to_string(self->GetId()), ec.message());
                self->Stop();
                return;
            }

            {
                std::lock_guard<std::mutex> sendQueueMutex(self->_sendTcpQueueMutex);
                if(self->_sendTcpQueue.empty())
                {
                    self->_isWriting = false;
                    return;
                }
            }

            self->DoSendAsyncTcpLoop();
        }
    }));
}