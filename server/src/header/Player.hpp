#pragma once
#include "Vector3.hpp"
#include <deque>
#include <cstdint>

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
