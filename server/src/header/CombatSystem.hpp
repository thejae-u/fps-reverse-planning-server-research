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

constexpr std::int16_t TARGET_KILLS = 100;

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

    void OnKill(
        uuids::uuid shooterId,
        Player& shooter,
        uuids::uuid victimId,
        const Player& victim,
        std::vector<TeamInfo>& teamInfos);

    LagCompensator& GetLagCompensator() { return _lagCompensator; }
    const LagCompensator& GetLagCompensator() const { return _lagCompensator; }

private:
    uuids::uuid _roomId;
    LagCompensator _lagCompensator;
    static constexpr float HIT_RADIUS = 0.45f;          // 클라이언트 CharacterController 반지름(0.4m) + 스킨 여유(0.05m)
    static constexpr float CAPSULE_BOTTOM_OFFSET = 0.4f; // 캡슐 하단 구 중심 높이 (0.4 - 0.45 ≈ 0.0m 발끝)
    static constexpr float CAPSULE_TOP_OFFSET = 1.4f;    // 캡슐 상단 구 중심 높이 (1.4 + 0.45 = 1.85m 머리끝)
    static constexpr float SHOOT_EYE_HEIGHT = 1.6f;      // 클라이언트 CameraHolder 눈높이 (1.6m)
};
