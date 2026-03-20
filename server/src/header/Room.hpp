#pragma once

#include <functional>
#include <memory>
#include <mutex>
#include <spdlog/spdlog.h>
#include <string>
#include <unordered_map>
#include <uuid.h>

#include "Packet.pb.h"
using namespace Protocol;

class Session;

class Room : public std::enable_shared_from_this<Room>
{
private:
    struct SecretKey {};

public:
    explicit Room(SecretKey, uuids::uuid roomId) : _roomId(roomId) {}
    ~Room()
    {
        spdlog::info("room {} destroyed", uuids::to_string(_roomId));
    }

    static auto Create(uuids::uuid roomId)
    {
        auto newRoom = std::make_shared<Room>(SecretKey{}, roomId);
        return newRoom;
    }

public:
    void Stop();
    void AddSession(uuids::uuid sessionId, std::shared_ptr<Session> session);
    void RemoveSession(std::shared_ptr<Session> removeSession);
    void Broadcast(std::shared_ptr<Packet> packet);

    uuids::uuid GetId() const
    {
        return _roomId;
    }

    using RemoveRoomCallback = std::function<void(const std::shared_ptr<Room>&)>;
    void SetRemoveRoomCallback(RemoveRoomCallback handler);

private:
    uuids::uuid _roomId;

    std::unordered_map<uuids::uuid, std::shared_ptr<Session>> _sessions;
    std::mutex _sessionsMutex;

    RemoveRoomCallback _removeRoomFromMatchingHandler;
};