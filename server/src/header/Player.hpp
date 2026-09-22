#pragma once
#include "Vector3.hpp"
#include <array>
#include <cstdint>

constexpr std::size_t SNAPSHOT_BUFFER_SIZE = 64;
constexpr std::size_t SNAPSHOT_BUFFER_MASK = SNAPSHOT_BUFFER_SIZE - 1; // 63

struct PlayerSnapshot
{
    std::size_t tick = 0;
    Vector3 position = Vector3(); // tick 당시 위치
};

enum class TeamType
{
    None = 0,
    TeamA = 1,
    TeamB = 2,
    Draw = 3,
};

constexpr float GRAVITY = 9.8f;
constexpr float DELTA_TIME = 0.05f;
constexpr float JUMP_SPEED = 5.0f;
constexpr float BASE_MOVE_SPEED = 10.0f;
constexpr std::int32_t MAX_SPEED = 20;

struct DamageResult
{
    bool isDead = false;
    std::int16_t currentHp = 100;
    std::int16_t deaths = 0;
};

class Player
{
public:
    Player()
        : teamType(TeamType::None), position(), velocity(), isGrounded(true),
          hp(100), ammo(30), attackPower(10), kill(0), death(0), assist(0), damage(0), heal(0), guard(0), lastRecordedTick(0)
    {
    }
    
public:
    // Movement & Physics
    void Move(Vector3 direction, std::int32_t speed);
    void Jump();
    void SimulatePhysics(float dt, float gravity = GRAVITY);
    void RecordSnapshot(std::size_t tick, std::size_t maxHistory = 60);

    // Combat & Stats
    DamageResult TakeDamage(std::int32_t amount);
    void AddDamageDealt(std::int32_t amount) { damage += amount; }
    void AddKill() { kill++; }
    
public:
    TeamType teamType;
    Vector3 position;
    Vector3 velocity;
    bool isGrounded;

    std::int16_t hp;
    std::int16_t ammo;
    std::int16_t attackPower;

    std::int16_t kill;
    std::int16_t death;
    std::int16_t assist;

    std::int32_t damage;
    std::int32_t heal;
    std::int32_t guard;

    std::size_t lastRecordedTick = 0;
    std::array<PlayerSnapshot, SNAPSHOT_BUFFER_SIZE> positionHistory{};
    
};

struct PlayerStat
{
    TeamType teamType;
    uuids::uuid id;
    std::int16_t kill;
    std::int16_t death;
    std::int16_t assist;

    std::int32_t damage;
    std::int32_t heal;
    std::int32_t guard;

    PlayerStat(const uuids::uuid id, const Player& player)
        : teamType(player.teamType), id(id), kill(player.kill), death(player.death), assist(player.assist),
          damage(player.damage), heal(player.heal), guard(player.guard)
    {
    }
};