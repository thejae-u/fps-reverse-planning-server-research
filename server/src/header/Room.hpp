#pragma once

#include <memory>
#include <mutex>
#include <spdlog/spdlog.h>
#include <string>
#include <unordered_map>
#include <uuid.h>

class Session;

class Room : public std::enable_shared_from_this<Room>
{
private:
    struct SecretKey {};

public:
    Room(SecretKey, uuids::uuid roomId) : _roomId(roomId) {}
    ~Room() = default;
    static auto Create(uuids::uuid roomId)
    {
        auto newRoom = std::make_shared<Room>(SecretKey{}, roomId);
        return newRoom;
    }

public:
    void AddSession(uuids::uuid sessionId, std::shared_ptr<Session> session);
    void RemoveSession(std::shared_ptr<Session> removeSession);
    void Broadcast(/*packet*/);
    uuids::uuid GetId()
    {
        return _roomId;
    }

private:
    uuids::uuid _roomId;

    std::unordered_map<uuids::uuid, std::shared_ptr<Session>> _sessions;
    std::mutex _sessionsMutex;
};