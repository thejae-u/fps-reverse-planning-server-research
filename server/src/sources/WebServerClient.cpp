#include "WebServerClient.hpp"

#include <regex>

WebServerClient::WebServerClient(const size_t id, asio::io_context& io, const std::weak_ptr<ConnectionPool>& connectionPool) 
    : _connectionPool(connectionPool), _isInUse(false), _id(id), _sock(std::make_shared<asio::ip::tcp::socket>(io))
{
    UpdateActivityTime();
}

void WebServerClient::Rent()
{
    _isInUse = true;
    UpdateActivityTime();
}

void WebServerClient::Return()
{
    _isInUse = false;
}

void WebServerClient::OnDisconnected()
{
    _sock->close();
    _sock = nullptr;
    
    if(auto connectionPool = _connectionPool.lock())
    {
        connectionPool->NotifyExpired(shared_from_this());
    }
}

void WebServerClient::SendAsync(std::shared_ptr<std::string> sendByte)
{
    // network code with asio
    _sock->async_send(asio::buffer(*sendByte), [sendByte, weakSelf = weak_from_this()](const std::error_code& ec, std::size_t) {
        if(auto self = weakSelf.lock())
        {
            if(ec)
            {
                if(ec == asio::error::operation_aborted || ec == asio::error::connection_aborted || ec == asio::error::connection_reset)
                    spdlog::info("internal connection disconnected");
                else
                    spdlog::error("internal connection excepted: {}", ec.message());
                
                self->OnDisconnected();
                return;
            }
            
            self->UpdateActivityTime();
        }
    });
}

void WebServerClient::ReceiveAsync()
{
    auto buffer = std::make_shared<std::string>(BUF_SIZE, '\0');
    _sock->async_read_some(asio::buffer(*buffer), [buffer, weakSelf = weak_from_this()](const std::error_code& ec, std::size_t) {
        if(auto self = weakSelf.lock())
        {
            if(ec)
            {
                if(ec == asio::error::operation_aborted || ec == asio::error::connection_reset || ec == asio::error::connection_reset)
                    spdlog::info("internal connection disconnected");
                else
                    spdlog::error("internal connection excepted: {}", ec.message());
            
                self->OnDisconnected();
                return;
            }    
            
            self->UpdateActivityTime();
            self->ProcessAsync(buffer);
        }
    });
}

void WebServerClient::ProcessAsync(std::shared_ptr<std::string> receiveByte)
{
}

bool WebServerClient::IsValid() const
{
    return _id != 0 && _sock->is_open();
}
