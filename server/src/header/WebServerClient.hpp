#pragma once

#include <atomic>
#include <memory>
#include <spdlog/spdlog.h>
#include <stduuid/uuid.h>
#include <asio.hpp>

#include "ConnectionPool.hpp"

class WebServerClient : std::enable_shared_from_this<WebServerClient>
{
private:
    static constexpr std::int32_t _bufSize = 1024;
public:
    explicit WebServerClient();
    ~WebServerClient()
    {
        spdlog::info("web server client: {} destroyed", _id);
    }

public:
    std::shared_ptr<asio::ip::tcp::socket> GetSocket() { return _sock; }
    void SendAsync(std::shared_ptr<std::string> sendByte);
    void ReceiveAsync();
    void ProcessAsync(std::shared_ptr<std::string> receiveByte);

public:
    void Init(int id, asio::io_context& io);
    std::size_t GetId() const { return _id; }
    bool IsValid() const;
    bool IsInUse() const { return _isInUse.load(); }
    void Return();


private:
    std::weak_ptr<ConnectionPool> _connectionPool;
    
    std::atomic<bool> _isInUse;
    std::size_t _id;
    std::shared_ptr<asio::ip::tcp::socket> _sock;

private:
    void OnDisconnected();
};
