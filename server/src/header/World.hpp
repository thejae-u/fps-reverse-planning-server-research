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

class Session;
class Room;

constexpr float GRAVITY = 9.8f;
constexpr float DELTA_TIME = 0.05f;
constexpr float JUMP_SPEED = 5.0f;

struct Vector3
{
    float x;
    float y;
    float z;

    Vector3 operator+(const Vector3& other) const
    {
        return { x + other.x, y + other.y, z + other.z };
    }

    Vector3 operator*(float scalar) const
    {
        return { x * scalar, y * scalar, z * scalar };
    }

    Vector3& operator+=(const Vector3& other)
    {
        x += other.x;
        y += other.y;
        z += other.z;
        return *this;
    }

    bool operator==(const Vector3& other)
    {
        return this->x == other.x && this->y == other.y && this->z == other.z;
    }
    
    std::string to_string() const
    {
        return "(" + std::to_string(x) + ", " + std::to_string(y) + ", " + std::to_string(z) + ")";
    }

    Vector3()
        : x(0), y(0), z(0)
    {
    }

    Vector3(const float x, const float y, const float z)
        : x(x), y(y), z(z)
    {
    }
};

struct PlayerSnapshot
{
    std::size_t tick;
    Vector3 position; // tick 당시 위치
};

struct Player
{
    Vector3 position;
    Vector3 velocity;
    bool isGrounded;

    std::int16_t hp;
    std::int16_t ammo;

    std::int16_t kill;
    std::int16_t death;
    std::int16_t assist;

    std::int32_t damage;
    std::int32_t heal;
    std::int32_t guard;
    
    std::deque<PlayerSnapshot> positionHistory;

    Player()
        : position(), velocity(), isGrounded(true), hp(100), ammo(30), kill(0), death(0), assist(0), damage(0), heal(0), guard(0)
    {
    }
};

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

    // Added metrics for test/monitoring
    void ClearMetrics();
    void GetMetrics(std::int64_t& minUs, std::int64_t& maxUs, double& avgUs);
    std::size_t GetTickCount() const { return _tickCount.load(); }
    void PrintScoreboard();

private:
    void ScheduleNextTick();
    void Update();
    void ProcessQueue();
    void UpdateState();
    
private:
    void Move(uuids::uuid player, Vector3 direction, std::int32_t speed);
    void Jump(uuids::uuid player);
    void Shoot(uuids::uuid shooterId, Vector3 direction, std::size_t targetTick);
    

private:
    static constexpr std::int32_t MAX_SPEED = 20;
    uuids::uuid _roomId;

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