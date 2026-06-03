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
    std::uint16_t _internalPort = 9000;

public:
    explicit ConnectionPool(SecretKey, const std::shared_ptr<IOManager>& ioManager, const std::size_t poolSize)
        : _ioManager(ioManager), _poolSize(poolSize), _tcpEndpoint(asio::ip::tcp::v4(), 9000), _acceptor(ioManager->GetIoContext(), _tcpEndpoint)
    {

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
    std::shared_ptr<WebServerClient> Rent();
    void Return(std::shared_ptr<WebServerClient> client);
    void NotifyExpired(std::shared_ptr<WebServerClient> client);

private:
    std::shared_ptr<IOManager> _ioManager;
    std::size_t _poolSize;
    std::size_t _poolIdCount = 1;
    std::mutex _poolMutex;
    std::map<int, std::shared_ptr<WebServerClient>> _pool;

    asio::ip::tcp::endpoint _tcpEndpoint;
    asio::ip::tcp::acceptor _acceptor;

private:
    void AcceptWebServerClientAsync();
};