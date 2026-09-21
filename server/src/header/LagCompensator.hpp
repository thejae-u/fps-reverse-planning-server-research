#pragma once

#include <unordered_map>
#include <memory>
#include <uuid.h>
#include <limits>
#include <cmath>
#include <new>

#include "Vector3.hpp"
#include "Player.hpp"

struct RewindData
{
    uuids::uuid id;         // 플레이어 id
    Player* player;         // 플레이어 포인터
    Vector3 originPosition; // 현재 위치
    Vector3 rewindPosition; // 과거 스냅샷 위치
    bool isHit;             // Hit 판정 플래그
};

class LagCompensator
{
public:
    LagCompensator() = default;
    ~LagCompensator() = default;

    std::vector<RewindData> Rewind(
        const std::unordered_map<uuids::uuid, std::unique_ptr<Player>>& players,
        uuids::uuid shooterId,
        std::size_t targetTick);
};