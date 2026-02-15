#pragma once

#include <memory>
#include <unordered_map>

class Session;

class Room : public std::enable_shared_from_this<Room>
{
public:
    Room() = default;
    ~Room() = default;

public:
    void AddSession(std::string sessionId, std::shared_ptr<Session> session);

private:
    std::unordered_map<std::string, std::shared_ptr<Session>> _sessions;
};
