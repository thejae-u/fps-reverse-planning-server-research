#include "TestMode.h"
#include "Player.hpp"
#include "LagCompensator.hpp"
#include "CombatSystem.hpp"
#include "GameResult.hpp"
#include "World.hpp"
#include "Room.hpp"
#include "IOManager.hpp"
#include "PacketPool.hpp"
#include "Packet.pb.h"

#include <uuid.h>
#include <spdlog/spdlog.h>
#include <memory>
#include <unordered_map>
#include <vector>
#include <thread>
#include <chrono>
#include <random>
#include <iostream>
#include <string>
#include <atomic>

namespace {

bool TestPlayerMovementAndPhysics()
{
    spdlog::info("[TEST] Running TestPlayerMovementAndPhysics...");
    bool passed = true;
    Player player;

    // 1. 초기 상태 확인
    if (player.position.y != 0.0f || !player.isGrounded)
    {
        spdlog::error("  [FAIL] Initial position/grounded state check failed");
        passed = false;
    }

    // 2. 이동 및 최대 속도 제한(MAX_SPEED = 20)
    player.Move(Vector3(1.0f, 0.0f, 0.0f), 50);
    if (player.velocity.x != static_cast<float>(MAX_SPEED))
    {
        spdlog::error("  [FAIL] Speed clamp failed: expected {}, got {}", MAX_SPEED, player.velocity.x);
        passed = false;
    }

    // 3. 음수 속도 클램프(0)
    player.Move(Vector3(1.0f, 0.0f, 0.0f), -10);
    if (player.velocity.x != 0.0f)
    {
        spdlog::error("  [FAIL] Negative speed clamp failed: velocity.x = {}", player.velocity.x);
        passed = false;
    }

    // 4. 점프 확인
    player.Jump();
    if (player.isGrounded || player.velocity.y != JUMP_SPEED)
    {
        spdlog::error("  [FAIL] Jump failed: velocity.y = {}, isGrounded = {}", player.velocity.y, player.isGrounded);
        passed = false;
    }

    // 5. 공중 물리 시뮬레이션 (dt = 0.05s)
    player.SimulatePhysics(0.05f, GRAVITY);
    if (player.position.y <= 0.0f)
    {
        spdlog::error("  [FAIL] Airborne physics failed: position.y = {}", player.position.y);
        passed = false;
    }

    // 6. 지면 착지 시뮬레이션
    for (int i = 0; i < 50; ++i)
    {
        player.SimulatePhysics(0.05f, GRAVITY);
    }
    if (!player.isGrounded || player.position.y != 0.0f || player.velocity.y != 0.0f)
    {
        spdlog::error("  [FAIL] Ground landing failed: y = {}, velY = {}, isGrounded = {}",
                      player.position.y, player.velocity.y, player.isGrounded);
        passed = false;
    }

    return passed;
}

bool TestPlayerDamageAndLifeCycle()
{
    spdlog::info("[TEST] Running TestPlayerDamageAndLifeCycle...");
    bool passed = true;
    Player player;

    // 1. 일반 대미지 적용 (100 -> 70)
    auto res1 = player.TakeDamage(30);
    if (res1.isDead || res1.currentHp != 70 || player.hp != 70 || player.death != 0)
    {
        spdlog::error("  [FAIL] Minor damage check failed: hp = {}", player.hp);
        passed = false;
    }

    // 2. 치명타 대미지 적용 및 리셋 (70 - 80 <= 0 -> 사망 처리, 데스 증가, 체력 100 리셋)
    auto res2 = player.TakeDamage(80);
    if (!res2.isDead || player.death != 1 || player.hp != 100)
    {
        spdlog::error("  [FAIL] Fatal damage check failed: isDead = {}, death = {}, hp = {}",
                      res2.isDead, player.death, player.hp);
        passed = false;
    }

    // 3. 통계 누적 확인
    player.AddDamageDealt(200);
    player.AddKill();
    if (player.damage != 200 || player.kill != 1)
    {
        spdlog::error("  [FAIL] Stats tracking failed: damage = {}, kill = {}", player.damage, player.kill);
        passed = false;
    }

    return passed;
}

bool TestPlayerSnapshotHistory()
{
    spdlog::info("[TEST] Running TestPlayerSnapshotHistory (64-entry Ring Buffer)...");
    bool passed = true;
    Player player;

    // 70개 틱(1 ~ 70) 스냅샷 기록
    for (std::size_t tick = 1; tick <= 70; ++tick)
    {
        player.position = Vector3(static_cast<float>(tick), 0.0f, 0.0f);
        player.RecordSnapshot(tick);
    }

    // 1. 고정 배열 크기 검증 (64)
    if (player.positionHistory.size() != SNAPSHOT_BUFFER_SIZE)
    {
        spdlog::error("  [FAIL] Snapshot array size check failed: size = {}", player.positionHistory.size());
        passed = false;
    }

    // 2. 최신 틱(70) 비트 마스킹 슬롯 저장 확인 (70 & 63 = 6)
    std::size_t slot70 = 70 & SNAPSHOT_BUFFER_MASK;
    if (player.positionHistory[slot70].tick != 70 || player.positionHistory[slot70].position.x != 70.0f)
    {
        spdlog::error("  [FAIL] Slot 70 check failed: tick = {}, x = {}",
                      player.positionHistory[slot70].tick, player.positionHistory[slot70].position.x);
        passed = false;
    }

    // 3. 아직 살아있는 과거 틱(틱 69: 69 & 63 = 5, 틱 7: 7 & 63 = 7) 확인
    std::size_t slot69 = 69 & SNAPSHOT_BUFFER_MASK;
    if (player.positionHistory[slot69].tick != 69)
    {
        spdlog::error("  [FAIL] Slot 69 check failed: tick = {}", player.positionHistory[slot69].tick);
        passed = false;
    }

    std::size_t slot7 = 7 & SNAPSHOT_BUFFER_MASK;
    if (player.positionHistory[slot7].tick != 7)
    {
        spdlog::error("  [FAIL] Slot 7 check failed: tick = {}", player.positionHistory[slot7].tick);
        passed = false;
    }

    return passed;
}

bool TestLagCompensatorRewindAndRestore()
{
    spdlog::info("[TEST] Running TestLagCompensatorRewindAndRestore...");
    bool passed = true;
    LagCompensator lagComp;
    std::unordered_map<uuids::uuid, std::unique_ptr<Player>> players;

    auto shooterId = uuids::uuid_system_generator{}();
    auto targetId = uuids::uuid_system_generator{}();

    players[shooterId] = std::make_unique<Player>();
    players[shooterId]->position = Vector3(0.0f, 0.0f, 0.0f);

    players[targetId] = std::make_unique<Player>();
    // 타깃이 틱 1부터 10까지 x=10 -> x=100 으로 이동
    for (std::size_t tick = 1; tick <= 10; ++tick)
    {
        players[targetId]->position = Vector3(static_cast<float>(tick) * 10.0f, 0.0f, 0.0f);
        players[targetId]->RecordSnapshot(tick);
    }
    Vector3 presentPos = players[targetId]->position;

    // 틱 4로 되감기 (x = 40 이어야 함)
    auto backup = lagComp.Rewind(players, shooterId, 4);

    if (backup.empty() || backup[0].rewindPosition.x != 40.0f)
    {
        spdlog::error("  [FAIL] Rewind position check failed: expected 40.0, got {}",
                      backup.empty() ? -1.0f : backup[0].rewindPosition.x);
        passed = false;
    }

    // 사격자 본인은 되감기 대상이 아니어야 함
    bool containsShooter = false;
    for (const auto& b : backup)
    {
        if (b.id == shooterId)
        {
            containsShooter = true;
            break;
        }
    }
    if (containsShooter)
    {
        spdlog::error("  [FAIL] Shooter should not be present in backup list");
        passed = false;
    }

    // RewindData 구조체 필드 정상성 검증 (targetId, player 포인터, originPosition, 초기 isHit=false 확인)
    if (backup.empty() || backup[0].id != targetId || backup[0].player != players[targetId].get() ||
        backup[0].originPosition.x != presentPos.x || backup[0].isHit != false)
    {
        spdlog::error("  [FAIL] RewindData fields mismatch");
        passed = false;
    }

    // 플레이어 인스턴스 원본 위치 불변성 검증 (Restore 없이도 원본 좌표가 100.0f로 유지되는지 확인)
    if (players[targetId]->position.x != presentPos.x)
    {
        spdlog::error("  [FAIL] Original player position mutated: expected {}, got {}",
                      presentPos.x, players[targetId]->position.x);
        passed = false;
    }

    // 만료된 과거 틱 O(1) 클램핑 검증: tick 0 요청 시 보관 중인 가장 오래된 tick 1(x=10.0)로 클램핑되어야 함
    auto backupClamped = lagComp.Rewind(players, shooterId, 0);
    if (backupClamped.empty() || backupClamped[0].rewindPosition.x != 10.0f)
    {
        spdlog::error("  [FAIL] Clamped rewind check failed: expected 10.0, got {}",
                      backupClamped.empty() ? -1.0f : backupClamped[0].rewindPosition.x);
        passed = false;
    }

    return passed;
}

bool TestCombatSystemRaycastHit()
{
    spdlog::info("[TEST] Running TestCombatSystemRaycastHit...");
    bool passed = true;
    auto roomId = uuids::uuid_system_generator{}();
    CombatSystem combatSystem(roomId);

    std::unordered_map<uuids::uuid, std::unique_ptr<Player>> players;
    std::vector<TeamInfo> teamInfos;
    teamInfos.emplace_back(TeamInfo(TeamType::TeamA));
    teamInfos.emplace_back(TeamInfo(TeamType::TeamB));

    auto shooterId = uuids::uuid_system_generator{}();
    auto inPathTargetId = uuids::uuid_system_generator{}();
    auto outPathTargetId = uuids::uuid_system_generator{}();

    // 사격자 (TeamA, 원점)
    players[shooterId] = std::make_unique<Player>();
    players[shooterId]->teamType = TeamType::TeamA;
    players[shooterId]->position = Vector3(0.0f, 0.0f, 0.0f);

    // 사격선 상의 타깃 (TeamB, z=10 위치)
    players[inPathTargetId] = std::make_unique<Player>();
    players[inPathTargetId]->teamType = TeamType::TeamB;
    players[inPathTargetId]->position = Vector3(0.0f, 0.0f, 10.0f);
    players[inPathTargetId]->RecordSnapshot(1);

    // 사격선에서 벗어난 타깃 (TeamB, x=50, z=10 위치)
    players[outPathTargetId] = std::make_unique<Player>();
    players[outPathTargetId]->teamType = TeamType::TeamB;
    players[outPathTargetId]->position = Vector3(50.0f, 0.0f, 10.0f);
    players[outPathTargetId]->RecordSnapshot(1);

    // 사격자가 +Z 방향(0, 0, 1)으로 사격 수행
    combatSystem.Shoot(shooterId, Vector3(0.0f, 0.0f, 1.0f), 1, players, teamInfos, std::weak_ptr<Room>{});

    // 1. 사격선 상 타깃 피격 확인 (HP 100 -> 90)
    if (players[inPathTargetId]->hp != 90)
    {
        spdlog::error("  [FAIL] In-path target hit failed: expected hp 90, got {}", players[inPathTargetId]->hp);
        passed = false;
    }

    // 2. 사격선 밖 타깃 빗나감 확인 (HP 100 유지)
    if (players[outPathTargetId]->hp != 100)
    {
        spdlog::error("  [FAIL] Out-of-path target hit failed: expected hp 100, got {}", players[outPathTargetId]->hp);
        passed = false;
    }

    // 3. 사격자의 누적 대미지 확인 (10)
    if (players[shooterId]->damage != 10)
    {
        spdlog::error("  [FAIL] Shooter damage tracking failed: expected 10, got {}", players[shooterId]->damage);
        passed = false;
    }

    // 4. TeamA 총 대미지 확인 (10)
    auto teamAIdx = static_cast<int>(TeamType::TeamA);
    if (teamInfos[teamAIdx].damages != 10)
    {
        spdlog::error("  [FAIL] Team damage tracking failed: expected 10, got {}", teamInfos[teamAIdx].damages);
        passed = false;
    }

    return passed;
}

} // namespace

void TestMode::RunTestMode()
{
    spdlog::info("==========================================");
    spdlog::info("      STARTING IN-GAME UNIT TESTS         ");
    spdlog::info("==========================================");

    int totalTests = 5;
    int passedTests = 0;

    if (TestPlayerMovementAndPhysics()) ++passedTests;
    if (TestPlayerDamageAndLifeCycle()) ++passedTests;
    if (TestPlayerSnapshotHistory()) ++passedTests;
    if (TestLagCompensatorRewindAndRestore()) ++passedTests;
    if (TestCombatSystemRaycastHit()) ++passedTests;

    spdlog::info("==========================================");
    spdlog::info(" TEST RESULT: {}/{} PASSED", passedTests, totalTests);
    spdlog::info("==========================================");

    if (passedTests == totalTests)
    {
        spdlog::info("All unit tests passed successfully!");
    }
    else
    {
        spdlog::error("Some unit tests failed!");
    }

    // 실제 지속 시뮬레이션 루프 실행
    RunSimulation();
}

void TestMode::RunSimulation()
{
    spdlog::info("==========================================");
    spdlog::info("   STARTING REAL-TIME SIMULATION LOOP     ");
    spdlog::info("==========================================");

    // 1. 패킷 풀 초기화
    IngamePacketPool::Init(500);
    NetworkPacketPool::Init(500);
    ByteBufferPool::Init(500);

    // 2. ASIO 및 World 인스턴스 생성
    // 2. IOManager 및 Room 인스턴스 생성
    auto ioManager = IOManager::Create("TestIO", 2, 2);
    auto matchId = uuids::uuid_system_generator{}();
    auto room = Room::Create(ioManager, matchId, "test-api", _testClientCount);
    auto* world = room->GetWorld();

    // 3. 가상 플레이어 세션 생성 및 초기화
    std::unordered_map<uuids::uuid, std::weak_ptr<Session>> fakeSessions;
    std::vector<uuids::uuid> playerIds;
    playerIds.reserve(_testClientCount);

    for (std::uint16_t i = 0; i < _testClientCount; ++i)
    {
        auto playerId = uuids::uuid_system_generator{}();
        playerIds.push_back(playerId);
        fakeSessions[playerId] = std::weak_ptr<Session>{};
    }

    world->Init(fakeSessions);

    // 4. World 고정 틱 시뮬레이션 시작 (16.6ms = 60 FPS)
    world->StartUpdate(room, std::chrono::microseconds(16666));

    spdlog::info("World simulation running for {} virtual clients.", _testClientCount);
    spdlog::info("Type 'quit' or 'q' and press Enter to stop simulation.");

    // 5. 가상 클라이언트 액션(이동, 점프, 사격) 백그라운드 주입 스레드
    std::atomic<bool> isRunning = true;
    std::thread botThread([&]() {
        std::mt19937_64 rng(std::random_device{}());
        std::uniform_int_distribution<int> actionDist(0, 9);
        std::uniform_real_distribution<float> dirDist(-1.0f, 1.0f);

        int tickCounter = 0;
        while (isRunning.load())
        {
            for (const auto& playerId : playerIds)
            {
                int action = actionDist(rng);

                if (action < 6)
                {
                    // [Action 1: Move]
                    Vector3 currentPos;
                    world->GetPlayerPosition(playerId, currentPos);

                    Protocol::MovePacket movePacket;
                    movePacket.set_playerid(uuids::to_string(playerId));
                    movePacket.set_originx(currentPos.x);
                    movePacket.set_originy(currentPos.y);
                    movePacket.set_originz(currentPos.z);

                    float dx = dirDist(rng);
                    float dz = dirDist(rng);
                    movePacket.set_dirx(dx);
                    movePacket.set_diry(0.0f);
                    movePacket.set_dirz(dz);

                    std::string data;
                    if (movePacket.SerializeToString(&data))
                    {
                        auto packet = IngamePacketPool::GetInstance()->Rent();
                        packet->set_sessionid(uuids::to_string(playerId));
                        packet->set_roomid(uuids::to_string(matchId));
                        packet->set_method(Protocol::IngameType::Move);
                        packet->set_data(data);
                        world->EnqueuePacket(packet);
                    }
                }
                else if (action < 8)
                {
                    // [Action 2: Jump]
                    auto packet = IngamePacketPool::GetInstance()->Rent();
                    packet->set_sessionid(uuids::to_string(playerId));
                    packet->set_roomid(uuids::to_string(matchId));
                    packet->set_method(Protocol::IngameType::Jump);
                    world->EnqueuePacket(packet);
                }
                else
                {
                    // [Action 3: Shoot]
                    Vector3 shootDir(dirDist(rng), 0.0f, dirDist(rng));
                    auto packet = IngamePacketPool::GetInstance()->Rent();
                    packet->set_sessionid(uuids::to_string(playerId));
                    packet->set_roomid(uuids::to_string(matchId));
                    packet->set_method(Protocol::IngameType::Shoot);
                    packet->set_clienttick(world->GetTickCount());
                    packet->set_data(reinterpret_cast<const char*>(&shootDir), sizeof(Vector3));
                    world->EnqueuePacket(packet);
                }
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            ++tickCounter;

            // 약 2초(40 틱)마다 스코어보드 현황 출력
            if (tickCounter % 40 == 0)
            {
                spdlog::info("[SIMULATION TICK: {}]", world->GetTickCount());
                world->PrintScoreboard();
            }
        }
    });

    // 6. 콘솔 입력 대기 (종료 신호 감지)
    std::string command;
    while (std::cin >> command)
    {
        if (command == "quit" || command == "q")
        {
            spdlog::info("Stopping simulation...");
            break;
        }
    }

    // 7. 정상 종료 처리
    isRunning.store(false);
    if (botThread.joinable())
    {
        botThread.join();
    }

    room->Stop();
    ioManager->Stop();

    spdlog::info("Simulation loop cleanly terminated.");
}