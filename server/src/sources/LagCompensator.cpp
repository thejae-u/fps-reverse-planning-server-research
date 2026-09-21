#include "LagCompensator.hpp"

std::vector<RewindData> LagCompensator::Rewind(
    const std::unordered_map<uuids::uuid, std::unique_ptr<Player>>& players,
    uuids::uuid shooterId,
    std::size_t targetTick)
{
    std::vector<RewindData> rewinds;
    rewinds.reserve(players.size()); // 메모리 자동 추가 방지 예약

    for(const auto& [id, player] : players)
    {
        // 자기 자신과 빈 플레이어 제외
        if(id == shooterId || !player)
            continue;

        // 현재 위치 백업
        auto newBackup = RewindData{ id, player.get(), player->position, player->position, false };

        // 스냅샷이 기록된 적이 없는 경우 현재 위치 유지
        if(player->lastRecordedTick == 0)
        {
            newBackup.rewindPosition = player->position;
            rewinds.emplace_back(newBackup);
            continue;
        }

        // 보관 중인 가장 오래된 유효 틱 계산 (최대 63틱 전까지 보관)
        const std::size_t oldestTick =
        player->lastRecordedTick >= SNAPSHOT_BUFFER_MASK ?
            player->lastRecordedTick - SNAPSHOT_BUFFER_MASK :
            1;

        if(targetTick >= oldestTick && targetTick <= player->lastRecordedTick)
        {
            // 1. 유효 범위 내 틱 -> O(1) 비트 마스킹 즉시 조회
            const std::size_t slot = targetTick & SNAPSHOT_BUFFER_MASK;
            newBackup.rewindPosition = player->positionHistory[slot].position;
        }
        else if(targetTick < oldestTick)
        {
            // 2. 너무 오래된 과거 틱(64틱 초과) -> 보관 중인 가장 오래된 스냅샷 위치로 O(1) 클램핑
            const std::size_t oldestSlot = oldestTick & SNAPSHOT_BUFFER_MASK;
            newBackup.rewindPosition = player->positionHistory[oldestSlot].position;
        }

        // 3. 아직 도달하지 않은 미래 틱 -> 현재 위치 유지
        // Do Nothing

        // 백업 데이터 저장
        rewinds.emplace_back(newBackup);
    }

    return rewinds;
}