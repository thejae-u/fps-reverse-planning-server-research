#pragma once

#include <unordered_map>
#include <memory>
#include <uuid.h>
#include <mutex>
#include <spdlog/spdlog.h>

class Session;

struct Vector3 
{
    std::int32_t x;
    std::int32_t y;
    std::int32_t z;

    Vector3 operator+(const Vector3& other) const
    {
        return { x + other.x, y + other.y, z + other.z };
    }
    Vector3 operator*(std::int32_t scalar) const
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

    Vector3() : x(0), y(0), z(0) {}
    Vector3(const std::int32_t x, const std::int32_t y, const std::int32_t z) : x(x), y(y), z(z) {}
};

struct Player
{
    Vector3 position;
    Vector3 velocity;

    std::int16_t hp;
    std::int16_t ammo;

    std::int16_t kill;
    std::int16_t death;
    std::int16_t assist;

    std::int32_t damage;
    std::int32_t heal;
    std::int32_t guard;

    Player() 
        : position(), velocity(), hp(0), ammo(0), kill(0), death(0), assist(0), damage(0), heal(0), guard(0) {}
};

class World
{
public:
    explicit World(uuids::uuid roomId) : _roomId(roomId), _playerSize(static_cast<std::size_t>(0)) {}
    ~World() 
    {
        spdlog::info("world(room id) {}: world destroyed", uuids::to_string(_roomId));
    }

    void Init(const std::vector<uuids::uuid> sessions);
    void Move(uuids::uuid player, Vector3 position);

private:
    uuids::uuid _roomId;

    std::size_t _playerSize;
    std::unordered_map<uuids::uuid, std::unique_ptr<Player>> _players;
    std::mutex _playerMutex;
};