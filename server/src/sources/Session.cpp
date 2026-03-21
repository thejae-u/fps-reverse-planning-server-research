#include "Session.hpp"
#include "asio.hpp"

void Session::Start()
{
    if(_disconnectCallback == nullptr)
    {
        spdlog::error("session {}: disconnect callback not set", uuids::to_string(GetId()));
        return;
    }
}

void Session::Stop()
{
    _socketPtr->close();
    if(_disconnectCallback == nullptr)
        return;

    _disconnectCallback(shared_from_this());
    _disconnectCallback = nullptr;
}

void Session::StartPortHandshaking()
{
    spdlog::info("{} handshake", uuids::to_string(_id));

    _ioManager->PostOnBlockingPool([weakSelf = weak_from_this()]() {
        if(auto self = weakSelf.lock())
        {
            self->ExchangeUdpPort();
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

void Session::SetNotifyDisconnectCallback(NotifyDisconnectCallback callback)
{
    _disconnectCallback = std::move(callback);
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

void Session::EnqueueSendPacket(const std::shared_ptr<Packet> data)
{
    _ioManager->PostOnBlockingPool([weakSelf = weak_from_this(), data]() {
        if(auto self = weakSelf.lock())
        {
            // seraizlie data
            int size = static_cast<int>(data->ByteSizeLong());
            Raw sendPacket;
            if (!data->SerializeToArray(sendPacket.data(), size))
            {
                spdlog::error("session {}: failed serialize send packet", uuids::to_string(self->GetId()));
                return;
            }

            // make raw packet include 2byte size header
            std::uint16_t sendNetSize = static_cast<std::uint16_t>(htons(size));
            auto sendRaw = std::make_shared<Raw>(sizeof(sendNetSize) + sendPacket.size());
            std::memcpy(sendRaw->data(), &sendNetSize, sizeof(sendNetSize));
            std::memcpy(sendRaw->data() + sizeof(sendNetSize), sendPacket.data(), sendPacket.size());

            std::lock_guard<std::mutex> sendQueueLock(self->_sendQueueMutex);
            self->_sendQueue.push(sendRaw);

            self->_sendQueueCv.notify_one();
        }
    });
}

void Session::DequeueSendPacket()
{
    std::unique_lock<std::mutex> sendQueueLock(_sendQueueMutex);
    _sendQueueCv.wait(sendQueueLock, [&] {
        return !_sendQueue.empty();
    });

    auto packet = _sendQueue.front();
    _sendQueue.pop();

    // send by udp
    if(_sendTo != nullptr)
        _sendTo(_clientUdpEp, std::move(packet));

    _ioManager->PostOnBlockingPool([weakSelf = weak_from_this()]() {
        if (auto self = weakSelf.lock())
        {
            self->DequeueSendPacket();
        }
    });
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

void Session::ExchangeUdpPort()
{
    std::string sendData;
    Packet sendPacket;

    sendPacket.set_type(PacketType::PortHandshake);
    sendPacket.set_data(std::to_string(_serverUdpPort));
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
        asio::write(*_socketPtr, asio::buffer(sendData), ec);

    // 3. receive port data size
    if(!ec)
        asio::read(*_socketPtr, asio::buffer(&netSize, sizeof(netSize)), ec);

    // 4. receive port real data
    if(!ec)
    {
        size = ntohs(netSize);
        receiveData.resize(size);

        asio::read(*_socketPtr, asio::buffer(receiveData), ec);
    }

    // if error occured once, move here
    if(ec)
    {
        spdlog::error("session {} error occured: {}", uuids::to_string(_id), ec.message());

        // disconnect immediately
        Stop();
        return;
    }

    // 5. parsing from received data
    if(!receivePacket.ParseFromArray(receiveData.data(), static_cast<int>(size)))
    {
        spdlog::error("session {} error occured: failed parse data", uuids::to_string(_id));
        Stop();
        return;
    }

    if(receivePacket.type() != PacketType::PortHandshake)
    {
        spdlog::error("session {} error occured: wrong packet", uuids::to_string(_id));
        return;
    }

    _clientUdpPort = static_cast<uint16_t>(std::stoi(receivePacket.data()));
    spdlog::info("session {} received port: {}", uuids::to_string(_id), _clientUdpPort);

    // Tcp read open
    ReadSizeAsync();

    // send work register
    _ioManager->PostOnBlockingPool([weakSelf = weak_from_this()]() {
        if(auto self = weakSelf.lock())
            self->DequeueSendPacket(); 
    });
}