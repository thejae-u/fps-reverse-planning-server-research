#include "Player.hpp"
#include <cmath>
#include <algorithm>

void Player::Move(Vector3 direction, std::int32_t speed)
{
    // Speed validation
    if(speed < 0)
    {
        speed = 0;
    }
    else if(speed > MAX_SPEED)
    {
        speed = MAX_SPEED;
    }

    // Direction validation
    if(std::abs(direction.x) > 1.0f || std::abs(direction.y) > 1.0f || std::abs(direction.z) > 1.0f)
    {
        return;
    }

    velocity = direction * speed;
}

void Player::Jump()
{
    if(isGrounded)
    {
        velocity.y = JUMP_SPEED;
        isGrounded = false;
    }
}

void Player::SimulatePhysics(float dt, float gravity)
{
    if(!isGrounded)
    {
        velocity.y -= gravity * dt;
    }

    position += velocity * dt;

    if(position.y <= 0.0f)
    {
        position.y = 0.0f;
        velocity.y = 0.0f;
        isGrounded = true;
    }
}

void Player::RecordSnapshot(std::size_t tick, std::size_t /*maxHistory*/)
{
    PlayerSnapshot snapshot;
    snapshot.tick = tick;
    snapshot.position = position;

    positionHistory[tick & SNAPSHOT_BUFFER_MASK] = snapshot;
    lastRecordedTick = tick;
}

DamageResult Player::TakeDamage(std::int32_t amount)
{
    DamageResult result;
    if(amount <= 0)
    {
        result.currentHp = hp;
        result.deaths = death;
        return result;
    }

    hp -= static_cast<std::int16_t>(amount);
    if(hp <= 0)
    {
        result.isDead = true;
        death++;
        hp = 100; // 사망 시 초기 체력으로 리셋
    }

    result.currentHp = hp;
    result.deaths = death;
    return result;
}