#pragma once

#include <atomic>
#include <memory>
#include <chrono>
#include <vector>
#include <spdlog/spdlog.h>
#include <stduuid/uuid.h>
#include <asio.hpp>

#include "Internal.pb.h"
#include "ConnectionPool.hpp"

class WebServerClient : std::enable_shared_from_this<WebServerClient>
{
private:
    static constexpr std::int32_t BUF_SIZE = 1024;
    static constexpr std::uint16_t HEADER_SIZE = 2;
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
    void ConnectAsync(const std::string& host, std::uint16_t port, std::function<void(const std::error_code&)> onConnected);
    void SendAsync(const Internal::GamePacket& packet);
    void ReceiveHeaderAsync();
    void UpdateActivityTime() { _lastActivityTime = std::chrono::steady_clock::now(); }
    auto GetLastActivityTime() const { return _lastActivityTime.load(); }

private:
    std::weak_ptr<ConnectionPool> _connectionPool;
    
    std::atomic<bool> _isInUse;
    std::size_t _id;
    std::shared_ptr<asio::ip::tcp::socket> _sock;
    
    std::atomic<std::chrono::steady_clock::time_point> _lastActivityTime;
    
    // Packet Receiving
    std::uint8_t _headerBuffer[HEADER_SIZE];
    void ReceiveBodyAsync(std::uint16_t bodySize);

    // Packet Handlers
    void HandleMatchCreateRequest(const Internal::MatchCreateRequest& req, uint32_t seqId);
    void HandleMatchCreateResponse(const Internal::MatchCreateResponse& res, uint32_t seqId);

private:
    void OnDisconnected();
};
