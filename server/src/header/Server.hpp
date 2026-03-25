#pragma once

#include <asio.hpp>
#include <memory>
#include <spdlog/spdlog.h>
#include <unordered_map>
#include <unordered_set>
#include <uuid.h>
#include <vector>
#include <queue>
#include <atomic>
#include <mutex>

#include "Packet.pb.h"
using namespace Protocol;

class IOManager;
class Matching;
class Room;
class Session;

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

    // Test Area
    static std::string ConvertType(PacketType type)
    {
        switch(type)
        {
        case PacketType::Ok:
            return "Ok";
        case PacketType::InvalidData:
            return "InvalidData";
        case PacketType::ErrorOccured:
            return "ErrorOccured";
        case PacketType::PortHandshake:
            return "PortHandshake";
        case PacketType::InfoHandshake:
            return "InfoHandshake";
        case PacketType::Ping:
            return "Ping";
        case PacketType::Ingame:
            return "Ingame";
        case PacketType::Autentication:
            return "Authentication";
        default:
            return "INVALID_TYPE_ERROR";
        }
    }

public:
    void Start();
    void Stop();

    void AcceptAsync();
    void AddRoom(std::shared_ptr<Room> room);
    void RemoveRoom(std::shared_ptr<Room> room);

private:
    using Raw = std::vector<unsigned char>;
    void EnqueueSendData(asio::ip::udp::endpoint ep, const std::shared_ptr<Raw> payload);
    void SendAsyncByUdp();
    void ReceiveAsyncByUdp();
    void ProcessPacket(std::shared_ptr<asio::ip::udp::endpoint> sender, std::uint16_t size, const unsigned char* data);
    std::shared_ptr<Room> GetRoom(uuids::uuid id)
    {
        std::lock_guard<std::mutex> roomsLock(_roomsMutex);
        auto room = _rooms.find(id);
        return room == _rooms.end() ? nullptr : room->second;
    }

private:
    std::shared_ptr<IOManager> _ioManager;
    asio::io_context::strand _strand;
    std::shared_ptr<Matching> _matching;

    asio::ip::tcp::endpoint _tcpEndpoint;
    asio::ip::tcp::acceptor _acceptor;

    asio::ip::udp::socket _udpSocket;
    asio::ip::udp::endpoint _udpEndpoint;

    uuids::uuid_system_generator _uuidGen;

    std::unordered_map<uuids::uuid /*room id*/, std::shared_ptr<Room> /*room object*/> _rooms;
    std::mutex _roomsMutex;

    std::unordered_map<uuids::uuid, std::shared_ptr<Session>> _sessions;
    std::mutex _sessionsMutex;

    std::queue<std::pair<asio::ip::udp::endpoint, std::shared_ptr<Raw>>> _payloadQueue;
    std::mutex _payloadQueueMutex;
    std::atomic<bool> _isSending;
};