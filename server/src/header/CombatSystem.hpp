#pragma once

#include <unordered_map>
#include <memory>
#include <vector>
#include <uuid.h>
#include <spdlog/spdlog.h>

#include "Vector3.hpp"
#include "Player.hpp"
#include "GameResult.hpp"
#include "LagCompensator.hpp"

class Room;

class CombatSystem
{
public:
    explicit CombatSystem(uuids::uuid roomId) : _roomId(roomId) {}
    ~CombatSystem() = default;

    void Shoot(
        uuids::uuid shooterId,
        Vector3 direction,
        std::size_t targetTick,
        std::unordered_map<uuids::uuid, std::unique_ptr<Player>>& players,
        std::vector<TeamInfo>& teamInfos,
        std::weak_ptr<Room> weakRoom);

    void OnHit(
        uuids::uuid hitId,
        std::int32_t damage,
        uuids::uuid shooterId,
        std::unordered_map<uuids::uuid, std::unique_ptr<Player>>& players,
        std::vector<TeamInfo>& teamInfos,
        std::weak_ptr<Room> weakRoom);

    LagCompensator& GetLagCompensator() { return _lagCompensator; }
    const LagCompensator& GetLagCompensator() const { return _lagCompensator; }

private:
    uuids::uuid _roomId;
    LagCompensator _lagCompensator;
    static constexpr float HIT_RADIUS = 3.0f;
};
