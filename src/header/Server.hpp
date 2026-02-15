#pragma once

#include <asio.hpp>
#include <memory>
#include <spdlog/spdlog.h>
#include <unordered_map>
#include <vector>

class IOManager;
class Room;

class Server : public std::enable_shared_from_this<Server>
{
private:
    struct SecretKey {};

public:
    explicit Server(SecretKey, std::shared_ptr<IOManager> ioManager, std::uint16_t port);
    ~Server() { spdlog::info("Server Successfully Destroyed"); }

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
    std::shared_ptr<IOManager> _ioManager;
    asio::ip::tcp::endpoint _serverEndpoint;
    asio::ip::tcp::acceptor _acceptor;

    std::unordered_map<std::string /*room id*/, std::shared_ptr<Room> /*room object*/> _rooms;
};