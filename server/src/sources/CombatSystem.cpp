#include "CombatSystem.hpp"
#include "Room.hpp"
#include "PacketPool.hpp"
#include "Packet.pb.h"

#include <cmath>
#include <algorithm>

void CombatSystem::Shoot(
    uuids::uuid shooterId,
    Vector3 direction,
    std::size_t targetTick,
    std::unordered_map<uuids::uuid, std::unique_ptr<Player>>& players,
    std::vector<TeamInfo>& teamInfos,
    std::weak_ptr<Room> weakRoom)
{
    // player 유효성 확인
    auto shooterIt = players.find(shooterId);
    if(shooterIt == players.end() || !shooterIt->second)
        return;

    // shooter 접근 가독성을 위한 캐싱
    auto& [id, shooter] = *shooterIt;

    // Rewind All Players
    auto rewinds = _lagCompensator.Rewind(players, id, targetTick);
    Vector3 shootOrigin = shooter->position;

    constexpr float HIT_RADIUS_SQ = HIT_RADIUS * HIT_RADIUS; // 충돌 구체 범위
    Vector3 normalizedDirection = direction.normalized();    // 방향 벡터 정규화

    // shooter 미포함 rewind 데이터
    for(auto& [rewindId, rewindPlayer, originPosition, rewindPosition, isHit] : rewinds)
    {
        // 같은 팀 제외
        if(rewindPlayer->teamType == shooter->teamType)
            continue;

        /*
         *  O = shootOrigin : 발사 중심
         *  C = rewindPosition : 적 중심 위치
         *  V = C - O : shooter 로부터 적의 방향 벡터
         *  D = normalized(shootDirection) : 정규화 된 발사선 방향 벡터
         *  t = V dot D : 적과 발사선의 내적 값 (Scalar, 음수면 체크 안함)
         *  P = O + tD : 발사선 상의 적의 최근접점 (발사선과 가장 가까운 벡터)
         *  ||P - C||^2 : P와 C의 최단거리 제곱
         */

        // V = enemy - shooter (shooter로부터 적의 방향 벡터)
        Vector3 v = rewindPosition - shootOrigin;

        // t = V dot D (shooter로부터 방향 벡터와 Shoot 방향 벡터 내적)
        float t = v.dot(normalizedDirection);

        // 음수면 뒤에 있음 (PASS)
        if(t < 0.0f)
            continue;

        // P = O + t * D
        Vector3 p = normalizedDirection * t + shootOrigin;

        // ||P - C||^2
        Vector3 diff = p - rewindPosition;   // P - C
        const float distSq = diff.dot(diff); // ||PC||^2 = PC dot PC (자기 자신의 내적 값은 제곱 크기)

        // Collide Check
        if(distSq <= HIT_RADIUS_SQ)
        {
            isHit = true;
        }
    }

    // 2. Broadcast Shoot (LagComp) packet first
    if(auto room = weakRoom.lock())
    {
        Protocol::LagCompPacket lagCompPacket;
        lagCompPacket.set_shooterid(uuids::to_string(shooterId));

        lagCompPacket.set_originx(shootOrigin.x);
        lagCompPacket.set_originy(shootOrigin.y);
        lagCompPacket.set_originz(shootOrigin.z);

        lagCompPacket.set_dirx(normalizedDirection.x);
        lagCompPacket.set_diry(normalizedDirection.y);
        lagCompPacket.set_dirz(normalizedDirection.z);

        for(const auto& [id, player, op, rp, isHit] : rewinds)
        {
            auto* targetMsg = lagCompPacket.add_targets();
            targetMsg->set_targetid(uuids::to_string(id));

            // 현재 위치
            targetMsg->set_presentx(op.x);
            targetMsg->set_presenty(op.y);
            targetMsg->set_presentz(op.z);

            // 되감겼던 위치
            targetMsg->set_rewoundx(rp.x);
            targetMsg->set_rewoundy(rp.y);
            targetMsg->set_rewoundz(rp.z);

            targetMsg->set_ishit(isHit);
        }

        std::string serializedLagComp;
        if(lagCompPacket.SerializeToString(&serializedLagComp))
        {
            auto ingamePacket = IngamePacketPool::GetInstance()->Rent();
            ingamePacket->set_sessionid(uuids::to_string(shooterId));
            ingamePacket->set_roomid(uuids::to_string(_roomId));
            ingamePacket->set_method(Protocol::IngameType::LagComp);
            ingamePacket->set_data(serializedLagComp);

            std::string serializedIngame;
            if(ingamePacket->SerializeToString(&serializedIngame))
            {
                auto sendPacket = NetworkPacketPool::GetInstance()->Rent();
                sendPacket->set_type(Protocol::PacketType::Ingame);
                sendPacket->set_data(serializedIngame);
                room->Broadcast(std::move(sendPacket));
            }
        }
    }

    // 3. Process Hits and Broadcast HitPackets after Shoot packet
    for(const auto& [id, player, op, rp, isHit] : rewinds)
    {
        if(isHit)
        {
            OnHit(id, shooter->attackPower, shooterId, players, teamInfos, weakRoom);
        }
    }
}

void CombatSystem::OnHit(
    uuids::uuid hitId,
    std::int32_t damage,
    uuids::uuid shooterId,
    std::unordered_map<uuids::uuid, std::unique_ptr<Player>>& players,
    std::vector<TeamInfo>& teamInfos,
    std::weak_ptr<Room> weakRoom)
{
    auto shooterIt = players.find(shooterId);
    if(shooterIt == players.end() || !shooterIt->second)
    {
        spdlog::warn("world(room id) {}: shooter {} not found on hit", uuids::to_string(_roomId), uuids::to_string(shooterId));
        return;
    }

    auto hitIt = players.find(hitId);
    if(hitIt == players.end() || !hitIt->second)
    {
        spdlog::warn("world(room id) {}: hit target {} not found", uuids::to_string(_roomId), uuids::to_string(hitId));
        return;
    }

    if(damage < 0)
    {
        spdlog::warn("world(room id) {}: invalid damage (damage is negative)", uuids::to_string(_roomId));
        return;
    }

    const auto& shooter = shooterIt->second;
    const auto shooterTeam = static_cast<int>(shooter->teamType);

    const auto& targetPlayer = hitIt->second;
    const auto targetPlayerTeam = static_cast<int>(targetPlayer->teamType);

    // damage 처리 (Player 내부 상태 갱신)
    DamageResult damageResult = targetPlayer->TakeDamage(damage);
    shooter->AddDamageDealt(damage);

    if(shooterTeam >= 0 && shooterTeam < static_cast<int>(teamInfos.size()))
    {
        teamInfos[shooterTeam].damages += damage;
    }

    // 플레이어 사망 시 통계 갱신
    if(damageResult.isDead)
    {
        if(targetPlayerTeam >= 0 && targetPlayerTeam < static_cast<int>(teamInfos.size()))
        {
            teamInfos[targetPlayerTeam].deaths++;
        }

        shooter->AddKill();
        if(shooterTeam >= 0 && shooterTeam < static_cast<int>(teamInfos.size()))
        {
            teamInfos[shooterTeam].kills++;
        }

        spdlog::info("world {}: player {} killed player {}",
                     uuids::to_string(_roomId), uuids::to_string(shooterId), uuids::to_string(hitId));
    }

    // Broadcast Hit Packet
    if(auto room = weakRoom.lock())
    {
        // 1. Create and populate HitPacket protobuf message
        Protocol::HitPacket hitPacket;
        hitPacket.set_hitplayerid(uuids::to_string(hitId));
        hitPacket.set_shooterid(uuids::to_string(shooterId));
        hitPacket.set_currenthp(damageResult.currentHp);
        hitPacket.set_deaths(damageResult.deaths);
        hitPacket.set_isdead(damageResult.isDead);
        hitPacket.set_damage(damage);

        // 2. Serialize HitPacket
        std::string serializedData;
        if(hitPacket.SerializeToString(&serializedData))
        {
            auto ingamePacket = IngamePacketPool::GetInstance()->Rent();
            ingamePacket->set_sessionid(uuids::to_string(hitId));
            ingamePacket->set_roomid(uuids::to_string(_roomId));
            ingamePacket->set_method(Protocol::IngameType::Hit);
            ingamePacket->set_data(serializedData);

            // 3. Serialize outer IngamePacket and broadcast
            std::string serializedIngame;
            if(ingamePacket->SerializeToString(&serializedIngame))
            {
                auto sendPacket = NetworkPacketPool::GetInstance()->Rent();
                sendPacket->set_type(Protocol::PacketType::Ingame);
                sendPacket->set_data(serializedIngame);
                room->Broadcast(std::move(sendPacket));
            }
        }

        // 4. Check target kills win condition
        if(damageResult.isDead && shooterTeam >= 0 && shooterTeam < static_cast<int>(teamInfos.size()))
        {
            if(teamInfos[shooterTeam].kills >= TARGET_KILLS)
            {
                spdlog::info("world {}: team {} reached target kills ({})! finishing match...",
                             uuids::to_string(_roomId), shooterTeam, TARGET_KILLS);
                room->OnMatchFinished();
            }
        }
    }
}