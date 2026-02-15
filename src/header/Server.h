#pragma once

#include <memory>
#include <asio.hpp>
#include <spdlog/spdlog.h>

class IOManager;

class Server
{
private:
    struct SecretKey
    {
    };

public:
    explicit Server(SecretKey, std::shared_ptr<IOManager> ioManager, std::uint16_t port);
    ~Server()
    {
        spdlog::info("Server Successfully Destroyed");
    }

    static std::shared_ptr<Server> Create(std::shared_ptr<IOManager> ioManager, std::uint16_t port)
    {
        auto newServer = std::make_shared<Server>(SecretKey{}, ioManager, port);
        return newServer;
    }

public:
    void Start();
    void Stop();

    void AcceptAsync();

    // Test Area
    void Test();

private:
    std::weak_ptr<IOManager> _ioManager;
    asio::ip::tcp::endpoint _serverEndpoint;
    asio::ip::tcp::acceptor _acceptor;
};