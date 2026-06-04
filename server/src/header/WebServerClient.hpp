#pragma once

#include <atomic>
#include <memory>
#include <chrono>
#include <spdlog/spdlog.h>
#include <stduuid/uuid.h>
#include <asio.hpp>

#include "ConnectionPool.hpp"

class WebServerClient : std::enable_shared_from_this<WebServerClient>
{
private:
    static constexpr std::int32_t BUF_SIZE = 1024;
public:
    explicit WebServerClient(const size_t id, asio::io_context& io, const std::weak_ptr<ConnectionPool>& connectionPool);
    ~WebServerClient()
    {
        spdlog::info("web server client: {} destroyed", _id);
    }

public:
    std::size_t GetId() const { return _id; }
    bool IsValid() const;
    bool IsInUse() const { return _isInUse.load(); }
    void Rent();
    void Return();
    
public: // Network
    std::shared_ptr<asio::ip::tcp::socket> GetSocket() { return _sock; }
    void SendAsync(std::shared_ptr<std::string> sendByte);
    void ReceiveAsync();
    void ProcessAsync(std::shared_ptr<std::string> receiveByte);
    void UpdateActivityTime() { _lastActivityTime = std::chrono::steady_clock::now(); }
    auto GetLastActivityTime() const { return _lastActivityTime.load(); }

private:
    std::weak_ptr<ConnectionPool> _connectionPool;
    
    std::atomic<bool> _isInUse;
    std::size_t _id;
    std::shared_ptr<asio::ip::tcp::socket> _sock;
    
    std::atomic<std::chrono::steady_clock::time_point> _lastActivityTime;

private:
    void OnDisconnected();
};
