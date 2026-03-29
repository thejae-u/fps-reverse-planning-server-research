#include "Session.hpp"

#include "asio.hpp"

void Session::StartTcpRead()
{
    if (_disconnectCallbacks.size() == 0)
    {
        spdlog::error("session {}: disconnect callback not set", uuids::to_string(GetId()));
        return;
    }

    // Tcp read open
    ReadSizeAsync();
}

void Session::Stop()
{
    _isValid = false;
    _socketPtr->close();

    if(_disconnectCallbacks.size() == 0)
        return;

    // execute all disconnect callback
    for(const auto& disconnectCallback : _disconnectCallbacks)
    {
        disconnectCallback(shared_from_this());
    }

    _disconnectCallbacks.clear();
}

void Session::Init()
{
    spdlog::info("{} handshake", uuids::to_string(_id));

    _ioManager->PostOnBlockingPool([weakSelf = weak_from_this()]() {
        if(auto self = weakSelf.lock())
        {
            self->SendSessionInfo();
        }
    });
}

void Session::SetRoomAndSendInfo(uuids::uuid roomId)
{
    _roomId = roomId;

    auto infoPacket = std::make_shared<Packet>();
    infoPacket->set_type(PacketType::InfoHandshake);
    infoPacket->set_data(std::format("{},{}", uuids::to_string(_roomId), uuids::to_string(_id)));

    SendAsync(std::move(infoPacket));
}

void Session::AddDisconnectCallback(NotifyDisconnectCallback callback)
{
    std::lock_guard<std::mutex> disconnectCallbacksLock(_disconnectCallbacksMutex);
    _disconnectCallbacks.push_back(callback);
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
    asio::async_read(*_socketPtr, asio::buffer(*receiveBuffer), asio::bind_executor(_strand, [weakSelf = weak_from_this(), receiveBuffer](const std::error_code& ec, std::size_t) {
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
            spdlog::info("session {} : read ok", uuids::to_string(self->GetId()));
            self->ReadSizeAsync();
        }
    }));
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
    std::shared_ptr<Raw> sendData;
    auto size = static_cast<int>(data->ByteSizeLong());
    if(!data->SerializeToArray(sendData->data(), size))
    {
        spdlog::error("session {}: failed to serialize tcp data", uuids::to_string(_id));
        return;
    }

    std::lock_guard<std::mutex> sendQueueLock(_sendTcpQueueMutex);
    _sendTcpQueue.push(sendData);

    if(!_isWriting)
    {
        DoSendAsyncTcpLoop();
    }
}

void Session::DoSendAsyncTcpLoop()
{
    std::shared_ptr<Raw> packet;
    std::lock_guard<std::mutex> sendQueueLock(_sendTcpQueueMutex);
    packet = _sendTcpQueue.front();
    _sendTcpQueue.pop();

    bool hasMore = !_sendTcpQueue.empty();
    asio::async_write(*_socketPtr, asio::buffer(*packet), asio::bind_executor(_strand, [weakSelf = weak_from_this(), packet, hasMore](const std::error_code& ec, std::size_t) {
        if(ec)
        {
            if(auto self = weakSelf.lock())
            {
                if(ec != asio::error::operation_aborted)
                    spdlog::error("session {} write loop error: {}", uuids::to_string(self->GetId()), ec.message());
                self->Stop();
            }
            return;
        }

        if(auto self = weakSelf.lock())
        {
            if(hasMore)
            {
                self->DoSendAsyncTcpLoop();
            }
        }
    }));
}

void Session::SendAsync(const std::shared_ptr<Packet> packet)
{
    // 1. serialize packet
    std::size_t size = packet->ByteSizeLong();
    auto sendBuffer = std::make_shared<std::vector<unsigned char>>(size);
    if(!packet->SerializeToArray(sendBuffer->data(), static_cast<int>(size)))
    {
        spdlog::error("session {} failed serailze packet");
        return;
    }

    // 2. calculate data size
    std::uint16_t dataSize = static_cast<std::uint16_t>(sendBuffer->size());
    std::uint16_t dataNetSize = htons(dataSize);

    // 3. data size send
    asio::async_write(*_socketPtr, asio::buffer(&dataNetSize, sizeof(dataNetSize)), asio::bind_executor(_strand, [weakSelf = weak_from_this(), sendBuffer, dataSize, dataNetSize](std::error_code ec, std::size_t) {
        if(ec)
        {
            if(auto self = weakSelf.lock())
            {
                spdlog::error("session {} send size async error occured: {}", uuids::to_string(self->GetId()), ec.message());
                self->Stop();
                return;
            }
        }

        if(auto self = weakSelf.lock())
        {
            spdlog::info("session {} send size complete", uuids::to_string(self->GetId()));

            // 4. real data send
            asio::async_write(*self->_socketPtr, asio::buffer(*sendBuffer), asio::bind_executor(self->_strand, [weakSelf, sendBuffer](const std::error_code& ec, std::size_t) {
                if(ec)
                {
                    if(auto self = weakSelf.lock())
                    {
                        spdlog::error("session {} send data async error occured: {}", uuids::to_string(self->GetId()), ec.message());
                        self->Stop();
                        return;
                    }
                }

                if(auto self = weakSelf.lock())
                {
                    spdlog::info("[test log] session {} send complete", uuids::to_string(self->GetId()));
                }
            }));
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