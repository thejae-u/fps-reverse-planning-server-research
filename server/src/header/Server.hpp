#pragma once

#include <array>
#include <asio.hpp>
#include <memory>
#include <spdlog/spdlog.h>
#include <unordered_map>
#include <unordered_set>
#include <uuid.h>
#include <vector>

class IOManager;
class Matching;
class Room;

constexpr std::uint16_t BUF_SIZE = 65535;

class Server : public std::enable_shared_from_this<Server>
{
private:
    struct SecretKey {};

public:
    explicit Server(SecretKey, std::shared_ptr<IOManager> ioManager, std::shared_ptr<Matching> matching, std::uint16_t port);
    ~Server() { spdlog::info("server successfully destroyed"); }

    static std::shared_ptr<Server> Create(std::shared_ptr<IOManager> ioManager, std::shared_ptr<Matching> matching, std::uint16_t port)
    {
        auto newServer = std::make_shared<Server>(SecretKey{}, ioManager, matching, port);
        return newServer;
    }

public:
    void Start();
    void Stop();

    void AcceptAsync();

private:
    void ReceiveAsyncByUdp();

private:
    std::shared_ptr<IOManager> _ioManager;
    std::shared_ptr<Matching> _matching;

    asio::ip::tcp::endpoint _tcpEndpoint;
    asio::ip::tcp::acceptor _acceptor;

    asio::ip::udp::socket _udpSocket;
    asio::ip::udp::endpoint _udpEndpoint;

    uuids::uuid_system_generator _uuidGen;

    std::unordered_map<uuids::uuid /*room id*/, std::shared_ptr<Room> /*room object*/> _rooms;
    std::mutex _roomsMutex;

    std::unordered_set<uuids::uuid> _sessions;
    std::mutex _sessionsMutex;
};