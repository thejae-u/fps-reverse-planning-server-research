#include "Session.hpp"

#include "asio.hpp"
#include "Listener.hpp"
#include "CustomUtility.hpp"

void Session::StartTcpRead()
{
    {
        std::lock_guard<std::mutex> lock(_disconnectCallbacksMutex);
        if(_disconnectCallbacks.empty())
        {
            spdlog::error("session {}: disconnect callback not set", uuids::to_string(_id));
            return;
        }
    }

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

    // 콜백들을 로컬로 복사하고 원본 맵을 비움 (순회 중 수정 방지)
    std::unordered_map<CallbackHandle, NotifyDisconnectCallback> callbacks;
    {
        std::lock_guard<std::mutex> lock(_disconnectCallbacksMutex);
        callbacks = std::move(_disconnectCallbacks);
        _disconnectCallbacks.clear();
    }

    // 락 외부에서 콜백 실행 (데드락 방지 및 안전한 순회)
    for(const auto& [handle, disconnectCallback] : callbacks)
    {
        if(disconnectCallback)
        {
            disconnectCallback(GetShared<Session>());
            spdlog::info("session {} disconnect handle {} called", uuids::to_string(_id), handle);
        }
    }
}

void Session::Init()
{
    spdlog::info("session{}: Init", uuids::to_string(_id));
    asio::post(_strand, [weakSelf = GetWeak<Session>()]() {
        if(auto self = weakSelf.lock())
        {
            self->_state = SessionState::Initializing;
            self->SendSessionInfo();
        }
    });
}

void Session::PunchUdpHole(const asio::ip::udp::endpoint& ep)
{
    auto expected = SessionState::Initializing;
    if(_state.compare_exchange_strong(expected, SessionState::WaitMatching))
    {
        _clientUdpEp = ep;
        spdlog::info("session {}: udp hole punched successfully. ip: {}, port: {}",
            uuids::to_string(_id), ep.address().to_string(), ep.port());
        
        auto ackPacket = std::make_shared<Packet>();
        ackPacket->set_type(PacketType::Authentication);
        
        AuthenticationPacket authResult;
        authResult.set_method(AuthenticationType::AuthenticationOk);
        authResult.set_sessionid(uuids::to_string(_id));
        
        std::string serialized;
        authResult.SerializeToString(&serialized);
        ackPacket->set_data(serialized);
        
        EnqueueUdpSendPacket(ackPacket);
    }
}

void Session::SetRoom(uuids::uuid roomId)
{
    _roomId = roomId;

    Matchmaking matchmakingPacket;
    matchmakingPacket.set_type(MatchmakingType::Matched);
    matchmakingPacket.set_sessionid(uuids::to_string(_id));
    matchmakingPacket.set_roomid(uuids::to_string(roomId));

    std::string serializedData;
    if(!matchmakingPacket.SerializeToString(&serializedData))
    {
        spdlog::error("session {}: serialize match making packet error in set_room()", uuids::to_string(_id));
        return;
    }

    auto sendPacket = std::make_shared<Packet>();
    sendPacket->set_type(PacketType::Match);
    sendPacket->set_data(serializedData);

    EnqueueTcpSendPacket(std::move(sendPacket));
}

CallbackHandle Session::AddDisconnectCallback(NotifyDisconnectCallback callback)
{
    std::lock_guard<std::mutex> disconnectCallbacksLock(_disconnectCallbacksMutex);
    _disconnectCallbacks[_callbackHandleCount] = callback;
    return _callbackHandleCount++;
}

void Session::RemoveDisconnectCallback(CallbackHandle handle)
{
    std::lock_guard<std::mutex> disconnectCallbacksLock(_disconnectCallbacksMutex);
    _disconnectCallbacks.erase(handle);
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
            if(auto self = weakSelf.lock())
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

        if(auto self = weakSelf.lock())
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
        if(ec)
        {
            if(auto self = weakSelf.lock())
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
            }
            return;
        }

        if(auto self = weakSelf.lock())
        {
            self->_ioManager->PostOnBlockingPool([weakSelf, receiveBuffer, dataSize]() {
                if(auto self = weakSelf.lock())
                {
                    self->EnqueueProcessPacket(std::move(receiveBuffer), dataSize);
                }
            });

            self->ReadSizeAsync();
        }
    }));
}

void Session::EnqueueProcessPacket(const std::shared_ptr<Raw>& data, const std::uint16_t size)
{
    spdlog::info("enqueue packet");
    auto packet = std::make_shared<Packet>();
    if(!packet->ParseFromArray(data->data(), size))
    {
        spdlog::error("session {}: parsing process packet error", uuids::to_string(_id));
        return;
    }

    std::lock_guard<std::mutex> processQueueLock(_processQueueMutex);
    _processQueue.push(packet);

    if(_isProcessing)
        return;

    _ioManager->PostOnBlockingPool([weakSelf = GetWeak<Session>()]() {
        if(auto self = weakSelf.lock())
            self->ProcessPacketAsync();
    });
}

// TODO : DoProcessPacketAsync로 빼서 실제 parsing, proccess 로직은 별도의 함수로 관리
void Session::ProcessPacketAsync()
{
    spdlog::info("process packet");
    std::lock_guard<std::mutex> processQueueLock(_processQueueMutex);
    while(!_processQueue.empty())
    {
        auto packet = _processQueue.front();
        _processQueue.pop();

        // matching sequence
        if(packet->type() == PacketType::Match)
        {
            auto dataSize = packet->data().size();
            auto matchmakingPacket = std::make_shared<Matchmaking>();
            if(!matchmakingPacket->ParseFromArray(packet->data().data(), dataSize))
            {
                spdlog::error("session {}: parsing match making packet error", uuids::to_string(_id));
                return;
            }

            if(uuids::uuid::from_string(matchmakingPacket->sessionid()) != _id)
            {
                spdlog::error("session {}: session id is not same ({})", uuids::to_string(_id), matchmakingPacket->sessionid());
                return;
            }

            _state = SessionState::WaitMatching; // Update State
            _ioManager->PostOnBlockingPool([weakSelf = GetWeak<Session>()]() {
                if(auto self = weakSelf.lock())
                {
                    if(auto listener = self->_weakListener.lock())
                    {
                        spdlog::info("session {}: request waiting for match", uuids::to_string(self->GetId()));
                        MatchingLastError e;
                        if(!listener->AddToMatchmakingQueue(self->GetId(), e))
                        {
                            if(e == MatchingLastError::FailedByExsists)
                            {
                                return;
                            }

                            // failed packet send
                        }

                        // send ok packet
                        Matchmaking sendMatchmakingPacket;
                        sendMatchmakingPacket.set_type(MatchmakingType::Waiting);
                        sendMatchmakingPacket.set_sessionid(uuids::to_string(self->GetId()));

                        std::string serializedData;
                        if(!sendMatchmakingPacket.SerializeToString(&serializedData))
                        {
                            spdlog::error("session {}: serialize match making packet error", uuids::to_string(self->GetId()));
                            return;
                        }

                        auto sendPacket = std::make_shared<Packet>();
                        sendPacket->set_type(PacketType::Match);
                        sendPacket->set_data(serializedData);
                        self->EnqueueTcpSendPacket(std::move(sendPacket)); // send matching complete to client
                        self->_isValid = true;                             // allow broadcast
                    }
                }
            });
        }
        else
        {
            spdlog::warn("session {}: income not implemented packet type", uuids::to_string(GetId()));
            return;
        }
    }
}

void Session::EnqueueUdpSendPacket(const std::shared_ptr<Packet> data)
{
    // serialize data
    auto size = static_cast<int>(data->ByteSizeLong());
    auto sendPacket = std::make_shared<Raw>(size);
    if(!data->SerializeToArray(sendPacket->data(), size))
    {
        spdlog::error("session {}: failed serialize send packet", uuids::to_string(GetId()));
        return;
    }

    std::uint16_t sendNetSize = static_cast<std::uint16_t>(htons(size));
    auto payload = std::make_shared<Raw>(sizeof(sendNetSize) + sendPacket->size());
    std::memcpy(payload->data(), &sendNetSize, sizeof(sendNetSize));
    std::memcpy(payload->data() + sizeof(sendNetSize), sendPacket->data(), sendPacket->size());

    if(_sendTo == nullptr)
    {
        spdlog::error("session {}: send handler is not set", uuids::to_string(GetId()));
        Stop();
        return;
    }

    // Udp Send
    _sendTo(_clientUdpEp, payload);
}

void Session::EnqueueTcpSendPacket(const std::shared_ptr<Packet> data)
{
    auto size = static_cast<int>(data->ByteSizeLong());
    auto sendData = std::make_shared<Raw>(size);

    if(!data->SerializeToArray(sendData->data(), size))
    {
        spdlog::error("session {}: failed to serialize tcp data", uuids::to_string(_id));
        return;
    }

    {
        std::lock_guard<std::mutex> sendQueueLock(_sendTcpQueueMutex);
        _sendTcpQueue.push(sendData);
    }

    if(_isWriting)
        return;

    _isWriting = true;
    DoSendAsyncTcpLoop();
}

void Session::DoSendAsyncTcpLoop()
{
    // Dequeue from send queue
    std::shared_ptr<Raw> dataBody;
    std::size_t queueSize = 0;
    {
        std::lock_guard<std::mutex> sendQueueLock(_sendTcpQueueMutex);
        dataBody = _sendTcpQueue.front();
        _sendTcpQueue.pop();
        queueSize = _sendTcpQueue.size();
    }

    // calculate size for payload
    std::uint16_t size = dataBody->size();
    std::uint16_t netSize = htons(size);
    auto totalSize = sizeof(size) + size;

    // make payload (size header + send data)
    auto payload = std::make_shared<Raw>(totalSize);
    std::memcpy(payload->data(), &netSize, sizeof(netSize));
    std::memcpy(payload->data() + sizeof(netSize), dataBody->data(), size);

    spdlog::info("session {} calling async_write with totalSize: {}, remaining queue: {}", uuids::to_string(GetId()), totalSize, queueSize);

    asio::async_write(*_socketPtr, asio::buffer(*payload), asio::bind_executor(_strand, [weakSelf = GetWeak<Session>(), payload, totalSize](const std::error_code& ec, std::size_t bytesTransferred) {
        if(auto self = weakSelf.lock())
        {
            if(ec)
            {
                if(ec != asio::error::operation_aborted)
                    spdlog::error("session {} write error: {}", uuids::to_string(self->GetId()), ec.message());
                self->Stop();
                return;
            }

            std::lock_guard<std::mutex> sendQueueMutex(self->_sendTcpQueueMutex);
            if(self->_sendTcpQueue.empty())
            {
                self->_isWriting = false;
                return;
            }

            self->DoSendAsyncTcpLoop();
        }
    }));
}

void Session::SendSessionInfo()
{
    auto sendPacket = std::make_shared<Packet>();
    sendPacket->set_type(PacketType::PortHandshake);
    sendPacket->set_data(std::format("{},{}", std::to_string(_serverUdpPort), uuids::to_string(GetId())));

    spdlog::info("session {} sending handshake info asynchronously", uuids::to_string(GetId()));
    EnqueueTcpSendPacket(std::move(sendPacket));

    StartTcpRead();
}