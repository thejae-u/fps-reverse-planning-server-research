#include "Session.hpp"

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

void Session::StartHandShaking()
{
    spdlog::info("{} handshake", uuids::to_string(_id));

    _ioManager->RegisterBlockingWork([weakSelf = weak_from_this()]() {
        if(auto self = weakSelf.lock())
        {
            self->ExchangeUdpPort();
        }
    });
}

void Session::SetRoom(uuids::uuid roomId)
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

void Session::ReadSizeAsync()
{
    _socketPtr->async_read_some(asio::buffer(&_readNetSize, sizeof(_readNetSize)), [weakSelf = weak_from_this()](const std::error_code& ec, std::size_t) {
        if(ec)
        {
            if(auto self = weakSelf.lock())
            {
                if(ec == asio::error::connection_aborted || ec == asio::error::operation_aborted || ec == asio::error::eof)
                {
                    spdlog::info("session {}: aborted... disconnect", uuids::to_string(self->GetId()));
                }
                else
                {
                    spdlog::info("session {}: read error... disconnect", uuids::to_string(self->GetId()));
                }

                self->Stop();
            }

            return;
        }

        if (auto self = weakSelf.lock())
        {
            std::uint16_t dataSize= ntohs(self->_readNetSize);
            self->ReadDataAsync(dataSize);
        }
    });
}

void Session::ReadDataAsync(const std::uint16_t& dataSize)
{
    auto receiveBuffer = std::make_shared<std::vector<unsigned char>>(dataSize);
    _socketPtr->async_read_some(asio::buffer(*receiveBuffer), [weakSelf = weak_from_this(), receiveBuffer](const std::error_code ec, std::size_t) {
        if (ec)
        {
            if (auto self = weakSelf.lock())
            {
                if (ec == asio::error::connection_aborted || ec == asio::error::operation_aborted || ec == asio::error::eof)
                {
                    spdlog::info("session {}: aborted... disconnect", uuids::to_string(self->GetId()));
                }
                else
                {
                    spdlog::info("session {}: read error... disconnect", uuids::to_string(self->GetId()));
                }

                self->Stop();
            }

            return;
        }

        if (auto self = weakSelf.lock())
        {
            spdlog::info("session {} : read ok", uuids::to_string(self->GetId()));
            self->ReadSizeAsync();
        }
    });
}

void Session::SendAsync(const std::shared_ptr<Packet> packet)
{
    // 1. serialize packet
    std::size_t size = packet->ByteSizeLong();
    auto sendBuffer = std::make_shared<std::vector<unsigned char>>(size);
    if (!packet->SerializeToArray(sendBuffer->data(), static_cast<int>(size)))
    {
        spdlog::error("session {} failed serailze packet");
        return;
    }

    // 2. calculate data size
    std::uint16_t dataSize = static_cast<std::uint16_t>(sendBuffer->size());
    std::uint16_t dataNetSize = htons(dataSize);

    // 3. data size send
    _socketPtr->async_write_some(asio::buffer(&dataNetSize, sizeof(dataNetSize)), [weakSelf = weak_from_this(), sendBuffer, dataSize, dataNetSize](std::error_code ec, std::size_t) {
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
            self->_socketPtr->async_write_some(asio::buffer(*sendBuffer), [weakSelf, sendBuffer](std::error_code ec, std::size_t) {
                if(ec)
                {
                    if(auto self = weakSelf.lock())
                    {
                        spdlog::error("session {} send data async error occured: {}", uuids::to_string(self->GetId()), ec.message());
                        self->Stop();
                        return;
                    }
                }

                if (auto self = weakSelf.lock())
                {
                    spdlog::info("[test log] session {} send complete", uuids::to_string(self->GetId()));
                }
            });
        }
    });
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
    _socketPtr->write_some(asio::buffer(&netSize, sizeof(netSize)), ec);

    // 2. send port real data 
    if(!ec)
        _socketPtr->write_some(asio::buffer(sendData), ec);

    // 3. receive port data size
    if(!ec)
        _socketPtr->receive(asio::buffer(&netSize, sizeof(netSize)), 0, ec);

    // 4. receive port real data
    if(!ec)
    {
        size = ntohs(netSize);
        receiveData.resize(size);

        _socketPtr->receive(asio::buffer(receiveData), 0, ec);
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
}