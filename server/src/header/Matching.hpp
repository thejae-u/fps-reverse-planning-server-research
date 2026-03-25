#pragma once

#include <condition_variable>
#include <memory>
#include <mutex>
#include <queue>
#include <stack>

#include <uuid.h>

#include "IOManager.hpp"
#include "Room.hpp"

class Server;
class Session;

constexpr std::uint8_t MATCHING_PLAYERS = 10;

class Matching : public std::enable_shared_from_this<Matching>
{
private:
    struct SecretKey {};

public:
    explicit Matching(SecretKey, std::shared_ptr<IOManager> ioManager) : _ioManager(ioManager), _isRunning(false) {}
    ~Matching()
    {
        spdlog::info("matching destroyed");
    }
    static std::shared_ptr<Matching> Create(std::shared_ptr<IOManager> ioManager)
    {
        auto newMatching = std::make_shared<Matching>(SecretKey{}, ioManager);
        return newMatching;
    }

public:
    void AddWaitSession(uuids::uuid waitSessionId, std::shared_ptr<Session> session);
    void Start();
    void Stop();

    using RoomCallback = std::function<void(const std::shared_ptr<Room>&)>;
    void SetRegisterRoomCallback(RoomCallback handler);
    void SetRemoveRoomCallback(RoomCallback handler);

private:
    void TryMatch();
    void RemoveSession(std::shared_ptr<Session> removeSession);
    void RemoveRoom(std::shared_ptr<Room> removeRoom);

private:
    std::shared_ptr<IOManager> _ioManager;
    uuids::uuid_system_generator _uuidGen;

    // waiting sessions
    std::deque<std::pair<uuids::uuid, std::shared_ptr<Session>>> _waitingQueue;
    std::mutex _waitingQueueMutex;

    // matched rooms
    std::deque<std::pair<uuids::uuid, std::shared_ptr<Room>>> _activeRooms;
    std::mutex _activeRoomsMutex;
    RoomCallback _registerRoomToServerHandler;
    RoomCallback _removeRoomFromServerHandler;

    std::atomic<bool> _isRunning;
};