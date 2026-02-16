#pragma once

#include <memory>
#include <unordered_map>
#include <string>
#include <mutex>
#include <uuid.h>

class Session;

class Room : public std::enable_shared_from_this<Room>
{
public:
    Room() {}
    ~Room() = default;

public:
    void AddSession(uuids::uuid sessionId, std::shared_ptr<Session> session);
    void RemoveSession(uuids::uuid sessionId);
    void Broadcast(/*packet*/);

private:
    std::unordered_map<uuids::uuid, std::shared_ptr<Session>> _sessions;
    std::mutex _sessionsMutex;
};