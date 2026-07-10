#pragma once

#include <unordered_map>
#include <memory>
#include <uuid.h>
#include <mutex>
#include <spdlog/spdlog.h>
#include <asio.hpp>
#include <queue>
#include <atomic>
#include <chrono>
#include "Packet.pb.h"
#include "Vector3.hpp"
#include "Player.hpp"

class Session;
class Room;

constexpr float GRAVITY = 9.8f;
constexpr float DELTA_TIME = 0.05f;
constexpr float JUMP_SPEED = 5.0f;
constexpr float BASE_MOVE_SPEED = 10.0f;

class World
{
public:
    explicit World(asio::io_context& ioContext, uuids::uuid roomId)
        : _roomId(roomId), _playerSize(static_cast<std::size_t>(0)), _timer(ioContext), _tickInterval(50), _isUpdating(false)
    {
    }

    ~World()
    {
        spdlog::info("world(room id) {}: world destroyed", uuids::to_string(_roomId));
    }

    void Init(const std::unordered_map<uuids::uuid, std::weak_ptr<Session>>& sessions);
    bool GetPlayerPosition(uuids::uuid playerId, Vector3& outPosition);
    std::unordered_map<uuids::uuid, Vector3> RewindPlayers(uuids::uuid shooterId, std::size_t targetTick);
    void RestorePlayers(const std::unordered_map<uuids::uuid, Vector3>& backup);

    void StartUpdate(std::weak_ptr<Room> weakRoom, std::chrono::microseconds interval = std::chrono::microseconds(16666));
    void StopUpdate();
    void EnqueuePacket(std::shared_ptr<Protocol::IngamePacket> packet);

    std::size_t GetTickCount() const { return _tickCount.load(); }
    
    // TEST MONITORING
    void PrintScoreboard();

private:
    void ScheduleNextTick();
    void Update();
    void ProcessQueue();
    void UpdateState();
    
public:
    void Hit(uuids::uuid hitId, std::int32_t damage, uuids::uuid shooterId);
    
private:
    void Move(uuids::uuid player, Vector3 direction, std::int32_t speed);
    void Jump(uuids::uuid player);
    void Shoot(uuids::uuid shooterId, Vector3 direction, std::size_t targetTick);
    void HitNoLock(uuids::uuid hitId, std::int32_t damage, uuids::uuid shooterId);
    
    std::unordered_map<uuids::uuid, Vector3> RewindPlayersNoLock(uuids::uuid shooterId, std::size_t targetTick);
    void RestorePlayersNoLock(const std::unordered_map<uuids::uuid, Vector3>& backup);

private:
    static constexpr std::int32_t MAX_SPEED = 20;
    uuids::uuid _roomId;
    
    // Session Info
    std::mutex _sessionsMutex;
    std::unordered_map<uuids::uuid, std::weak_ptr<Session>> _sessions;

    // Player updates
    std::size_t _playerSize;
    std::unordered_map<uuids::uuid, std::unique_ptr<Player>> _players;
    std::mutex _playerMutex;
    
    std::atomic<std::size_t> _tickCount = 0;

    // Tick update details
    asio::steady_timer _timer;
    std::chrono::microseconds _tickInterval;
    std::weak_ptr<Room> _weakRoom;
    std::atomic<bool> _isUpdating;

    // Input queue
    std::queue<std::shared_ptr<Protocol::IngamePacket>> _packetQueue;
    std::mutex _queueMutex;
    
    // Metrics storage
    std::vector<std::int64_t> _tickDurationsUs;
    std::mutex _metricsMutex;
};