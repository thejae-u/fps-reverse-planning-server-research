#include "Session.hpp"

void Session::Start()
{
    if(_disconnectCallback == nullptr)
    {
        spdlog::error("session {} : disconnect callback not set", uuids::to_string(GetId()));
        return;
    }

    ReadAsync();
}

void Session::Stop()
{
    _socketPtr->close();
    if(_disconnectCallback == nullptr)
        return;

    _disconnectCallback(shared_from_this());
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
                    spdlog::info("{} aborted... disconnect", uuids::to_string(self->GetId()));
                }
                else
                {
                    spdlog::info("{} read error... disconnect", uuids::to_string(self->GetId()));
                }

                self->Stop();
            }

            return;
        }

        if(auto self = weakSelf.lock())
            self->ReadAsync();
    });
}
