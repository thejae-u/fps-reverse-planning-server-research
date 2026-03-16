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
}

void Session::SetNotifyDisconnectCallback(NotifyDisconnectCallback callback)
{
    _disconnectCallback = std::move(callback);
}

void Session::ReadAsync()
{
    auto weakSelf(weak_from_this());
    _socketPtr->async_read_some(asio::buffer(&_readNetSize, sizeof(_readNetSize)), [weakSelf](const std::error_code& netSizeErrorCode, std::size_t) {
        if(netSizeErrorCode)
        {
            if(auto self = weakSelf.lock())
            {
                if(netSizeErrorCode == asio::error::connection_aborted || netSizeErrorCode == asio::error::operation_aborted || netSizeErrorCode == asio::error::eof)
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

        if(auto self = weakSelf.lock())
            self->ReadAsync();
    });
}

void Session::SendAsync()
{
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

    // first send size
    _socketPtr->write_some(asio::buffer(&netSize, sizeof(netSize)), ec);

    // send port data
    if(!ec)
        _socketPtr->write_some(asio::buffer(sendData), ec);

    // receive data size
    if(!ec)
        _socketPtr->receive(asio::buffer(&netSize, sizeof(netSize)), 0, ec);

    // receive port data
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

    // parsing from received data
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

    // exchange finish

    ReadAsync();
}