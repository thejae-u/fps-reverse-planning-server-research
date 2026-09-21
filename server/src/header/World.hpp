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
#include "GameResult.hpp"
#include "CombatSystem.hpp"

class Session;
class Room;

class World
{
public:
    explicit World(asio::io_context& ioContext, const uuids::uuid roomId)
    : _roomId(roomId), _combatSystem(roomId), _playerSize(static_cast<std::size_t>(0)), _timer(ioContext), _tickInterval(50), _isUpdating(false), _gen(_rd()),
      _dis(static_cast<int>(TeamType::TeamA), static_cast<int>(TeamType::TeamB)), _teamACount(0), _teamBCount(0)
    {
        _teamInfos.reserve(2);
        _teamInfos.emplace_back(TeamInfo(TeamType::TeamA));
        _teamInfos.emplace_back(TeamInfo(TeamType::TeamB));
    }

    ~World()
    {
        spdlog::info("world(room id) {}: world destroyed", uuids::to_string(_roomId));
    }

    void Init(const std::unordered_map<uuids::uuid, std::weak_ptr<Session>>& sessions);
    bool GetPlayerPosition(uuids::uuid playerId, Vector3& outPosition);

    void StartUpdate(std::weak_ptr<Room> weakRoom, std::chrono::microseconds interval = std::chrono::microseconds(16666));
    void StopUpdate();
    void EnqueuePacket(std::shared_ptr<Protocol::IngamePacket> packet);

    std::size_t GetTickCount() const { return _tickCount.load(); }
    std::unique_ptr<std::vector<PlayerStat>> GetPlayerStats()
    {
        std::lock_guard lock(_playerMutex);
        auto playerStats = std::make_unique<std::vector<PlayerStat>>();
        playerStats->reserve(_players.size());
        for (auto& [id, player] : _players)
        {
            PlayerStat playerStat(id, *player);
            playerStats->push_back(playerStat);
        }
        
        return playerStats;
    }
    
    // TEST MONITORING
    void PrintScoreboard();

private:
    void DivideTeam();
    void ScheduleNextTick();
    void Update();
    void ProcessQueue();
    void UpdateState();
    
public:
    void Hit(uuids::uuid hitId, std::int32_t damage, uuids::uuid shooterId);
    std::unique_ptr<GameResult> GetResult();
    
private:
    void Move(uuids::uuid player, Vector3 direction, std::int32_t speed);
    void Jump(uuids::uuid player);
    void Shoot(uuids::uuid shooterId, Vector3 direction, std::size_t targetTick);

private:
    uuids::uuid _roomId;
    CombatSystem _combatSystem;
    
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

    // random device for team separate 
    std::random_device _rd;
    std::mt19937_64 _gen;
    std::uniform_int_distribution<> _dis;

    std::vector<TeamInfo> _teamInfos;
    std::uint16_t _teamACount;
    std::uint16_t _teamBCount;
};