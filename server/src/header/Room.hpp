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

class IOManager;
class SessionManager;
class Session;

class Room : public std::enable_shared_from_this<Room>
{
private:
    struct SecretKey {};

public:
    explicit Room(SecretKey, std::shared_ptr<IOManager> ioManager, std::shared_ptr<SessionManager> sessionManager, uuids::uuid roomId);
    ~Room();

    static auto Create(std::shared_ptr<IOManager> ioManager, std::shared_ptr<SessionManager> sessionManager, uuids::uuid roomId)
    {
        auto newRoom = std::make_shared<Room>(SecretKey{}, ioManager, sessionManager, roomId);
        return newRoom;
    }

public:
    void WorldInit();
    void Stop();
    void AddSession(uuids::uuid sessionId, std::weak_ptr<Session> session);
    void RemoveSession(std::weak_ptr<Session> removeSession);
    void Broadcast(std::shared_ptr<Packet> packet);
    void EnqueuePacket(std::shared_ptr<IngamePacket> packet) const;

    uuids::uuid GetId() const
    {
        return _roomId;
    }

    using RemoveRoomCallback = std::function<void(const std::shared_ptr<Room>&)>;
    void SetRemoveRoomCallback(RemoveRoomCallback handler);

    World* GetWorld() const { return _world.get(); }

private:
    std::shared_ptr<IOManager> _ioManager;
    std::shared_ptr<SessionManager> _sessionManager;
    uuids::uuid _roomId;

    std::unordered_map<uuids::uuid, std::weak_ptr<Session>> _sessions;
    std::mutex _sessionsMutex;

    RemoveRoomCallback _removeRoomFromMatchingHandler;

    // World information
    std::unique_ptr<World> _world;
};