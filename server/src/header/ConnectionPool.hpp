#pragma once
#include <memory>
#include <map>
#include <mutex>

#include <asio.hpp>
#include "Base.hpp"
#include "IOManager.hpp"

class WebServerClient;

class ConnectionPool : public IBase
{
private:
    struct SecretKey {};

    // Internal Connection Port
    std::uint16_t _internalPort = 9100;

public:
    explicit ConnectionPool(SecretKey, const std::shared_ptr<IOManager>& ioManager, const std::size_t poolSize)
        : _ioManager(ioManager), _poolSize(poolSize),
          _tcpEndpoint(asio::ip::tcp::v4(), _internalPort), _acceptor(ioManager->GetIoContext()),
          _cleanupTimer(ioManager->GetIoContext())
    {
        if(!_ioManager)
        {
            throw std::runtime_error("connection pool: io manager is null");
        }
    }

    ~ConnectionPool() override
    {
        spdlog::info("connection pool successfully released");
    }

    static auto Create(std::shared_ptr<IOManager> ioManager, std::size_t poolSize)
    {
        return std::make_shared<ConnectionPool>(SecretKey{}, ioManager, poolSize);
    }

public:
    void Start() override;
    void Stop() override;
    void RentAsync(std::function<void(std::shared_ptr<WebServerClient>)> callback);
    void Return(std::shared_ptr<WebServerClient> client);
    void NotifyExpired(std::shared_ptr<WebServerClient> client);
    void InternalTest();

private:
    std::shared_ptr<IOManager> _ioManager;
    std::size_t _poolSize;
    std::size_t _poolIdCount = 1;
    std::mutex _poolMutex;
    std::map<std::size_t, std::shared_ptr<WebServerClient>> _pool;

    // Auth Server Connection Info
    std::string _authServerHost = "127.0.0.1";
    std::uint16_t _authServerPort = 9102;

    asio::ip::tcp::endpoint _tcpEndpoint;
    asio::ip::tcp::acceptor _acceptor;
    
    // client time-out
    const std::size_t _minPoolSize = 2;
    asio::steady_timer _cleanupTimer;
    const std::chrono::minutes _idleTimeout = std::chrono::minutes(5);

private:
    void AcceptWebServerClientAsync();
    void StartCleanupTimer();
};