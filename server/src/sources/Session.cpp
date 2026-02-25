#include "Session.hpp"

void Session::Start()
{
    if (_disconnectCallback == nullptr)
    {
        spdlog::error("session {} : disconnect callback not set", uuids::to_string(GetId()));
        return;
    }

    AsyncRead();
}

void Session::Stop()
{
    _socketPtr->close();
    _disconnectCallback(shared_from_this());
}

void Session::SetRoom()
{
}

void Session::SetNotifyDisconnectCallback(NotifyDisconnectCallback callback)
{
    _disconnectCallback = std::move(callback);
}

void Session::AsyncRead()
{
    auto weakSelf(weak_from_this());
    _socketPtr->async_read_some(asio::buffer(&_readNetSize, sizeof(_readNetSize)), [weakSelf](const std::error_code& netSizeErrorCode, std::size_t) {
        if(netSizeErrorCode)
        {
            if(auto sharedSelf = weakSelf.lock())
            {
                if(netSizeErrorCode == asio::error::connection_aborted || netSizeErrorCode == asio::error::operation_aborted || netSizeErrorCode == asio::error::eof)
                {
                    spdlog::warn("{} aborted... disconnect", uuids::to_string(sharedSelf->GetId()));
                }
                else
                {
                    spdlog::error("{} read error... disconnect", uuids::to_string(sharedSelf->GetId()));
                }

                sharedSelf->Stop();
            }

            return;
        }

        spdlog::info("read complete");
        if(auto shreadSelf = weakSelf.lock())
            weakSelf.lock()->AsyncRead();
    });
}
