#pragma once

#include <queue>
#include <functional>
#include <memory>
#include <mutex>
#include <atomic>
#include <spdlog/spdlog.h>
#include <string>
#include <unordered_map>
#include <uuid.h>
#include <asio.hpp>

#include "Packet.pb.h"
#include "World.hpp"
using namespace Protocol;

class Session;
class IOManager;

class Room : public std::enable_shared_from_this<Room>
{
private:
    struct SecretKey {};

public:
    explicit Room(SecretKey, std::shared_ptr<IOManager> ioManager, uuids::uuid roomId) : _ioManager(ioManager), _roomId(roomId), _world(std::make_unique<World>(roomId)) {}
    ~Room()
    {
        spdlog::info("room {} destroyed", uuids::to_string(_roomId));
    }

    static auto Create(std::shared_ptr<IOManager> ioManager, uuids::uuid roomId)
    {
        auto newRoom = std::make_shared<Room>(SecretKey{}, ioManager, roomId);
        return newRoom;
    }

public:
    void WorldInit();
    void Stop();
    void AddSession(uuids::uuid sessionId, std::shared_ptr<Session> session);
    void RemoveSession(std::shared_ptr<Session> removeSession);
    void Broadcast(std::shared_ptr<Packet> packet);
    void EnqueuePacket(std::shared_ptr<IngamePacket> packet);
    void DequeuePacketAsync();
    void PunchUdpHole(uuids::uuid sessionId, std::shared_ptr<asio::ip::udp::endpoint> udpEndpoint);

    uuids::uuid GetId() const
    {
        return _roomId;
    }

    using RemoveRoomCallback = std::function<void(const std::shared_ptr<Room>&)>;
    void SetRemoveRoomCallback(RemoveRoomCallback handler);

private:
    std::shared_ptr<IOManager> _ioManager;
    uuids::uuid _roomId;
    std::atomic<bool> _isRunning;

    std::unordered_map<uuids::uuid, std::shared_ptr<Session>> _sessions;
    std::mutex _sessionsMutex;

    RemoveRoomCallback _removeRoomFromMatchingHandler;

    // World information
    std::unique_ptr<World> _world;

private:
    std::queue<std::shared_ptr<IngamePacket>> _sendPacketQueue;
    std::mutex _packetQueueMutex;
    std::condition_variable _packetQueueCv;
};