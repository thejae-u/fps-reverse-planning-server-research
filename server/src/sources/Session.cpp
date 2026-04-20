#include "Session.hpp"

#include "asio.hpp"
#include "Listener.hpp"
#include "CustomUtility.hpp"

void Session::StartTcpRead()
{
    if(_disconnectCallbacks.size() == 0)
    {
        spdlog::error("session {}: disconnect callback not set", uuids::to_string(_id));
        return;
    }

    // Tcp read open
    ReadSizeAsync();
}

void Session::Stop()
{
    if(_state == SessionState::Invalid) // already stopped
        return;

    _isValid = false;
    _state = SessionState::Invalid;
    _socketPtr->close();

    if(_disconnectCallbacks.size() == 0)
        return;

    // execute all disconnect callback
    for(const auto& [handle, disconnectCallback] : _disconnectCallbacks)
    {
        std::lock_guard<std::mutex> callbacksLock(_disconnectCallbacksMutex);
        disconnectCallback(shared_from_this());
        spdlog::info("session {} disconnect handle {} called", uuids::to_string(_id), handle);
    }

    _disconnectCallbacks.clear();
}

void Session::Init()
{
    spdlog::info("session{}: Init", uuids::to_string(_id));
    _ioManager->PostOnBlockingPool([weakSelf = weak_from_this()]() {
        if(auto self = weakSelf.lock())
        {
            self->_state = SessionState::Initializing;
            self->SendSessionInfo();
        }
    });
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

void Session::RemoveDiscconectCallback(CallbackHandle handle)
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
    asio::async_read(*_socketPtr, asio::buffer(&_readNetSize, sizeof(_readNetSize)), asio::bind_executor(_strand, [weakSelf = weak_from_this()](const std::error_code& ec, std::size_t) {
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
    asio::async_read(*_socketPtr, asio::buffer(*receiveBuffer), asio::bind_executor(_strand, [weakSelf = weak_from_this(), receiveBuffer, dataSize](const std::error_code& ec, std::size_t) {
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

    _ioManager->PostOnBlockingPool([weakSelf = weak_from_this()]() {
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
            _ioManager->PostOnBlockingPool([weakSelf = weak_from_this()]() {
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

    std::lock_guard<std::mutex> sendQueueLock(_sendTcpQueueMutex);
    _sendTcpQueue.push(sendData);

    if(_isWriting)
        return;

    _isWriting = true;
    DoSendAsyncTcpLoop();
}

void Session::DoSendAsyncTcpLoop()
{
    // Dequeue from send queue
    std::shared_ptr<Raw> dataBody;
    {
        std::lock_guard<std::mutex> sendQueueLock(_sendTcpQueueMutex);
        dataBody = _sendTcpQueue.front();
        _sendTcpQueue.pop();
    }

    // calculate size for payload
    std::uint16_t size = dataBody->size();
    std::uint16_t netSize = htons(size);
    auto totalSize = sizeof(size) + size;

    // make payload (size header + send data)
    auto payload = std::make_shared<Raw>(totalSize);
    std::memcpy(payload->data(), &netSize, sizeof(netSize));
    std::memcpy(payload->data() + sizeof(netSize), dataBody->data(), size);

    asio::async_write(*_socketPtr, asio::buffer(*payload), asio::bind_executor(_strand, [weakSelf = weak_from_this(), payload](const std::error_code& ec, std::size_t) {
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
    std::string sendData;
    Packet sendPacket;

    sendPacket.set_type(PacketType::PortHandshake);
    sendPacket.set_data(std::format("{},{}", std::to_string(_serverUdpPort), uuids::to_string(GetId())));
    sendPacket.SerializeToString(&sendData);

    // reusable
    std::uint16_t size = static_cast<std::uint16_t>(sendData.size());
    std::uint16_t netSize = htons(size);

    std::vector<unsigned char> receiveData;
    Packet receivePacket;

    std::error_code ec;

    // 1. send port data size
    asio::write(*_socketPtr, asio::buffer(&netSize, sizeof(netSize)), ec);

    // 2. send port real data
    if(!ec)
    {
        spdlog::info("session {} send size complete", uuids::to_string(_id));
        asio::write(*_socketPtr, asio::buffer(sendData), ec);
    }

    // if error occured once, move here
    if(ec)
    {
        spdlog::error("session {} send error occured: {}", uuids::to_string(_id), ec.message());
        Stop();
        return;
    }

    StartTcpRead();
}