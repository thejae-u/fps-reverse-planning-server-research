#include "TestWorld.hpp"

#include <thread>
#include <cassert>
#include <chrono>
#include <vector>
#include <atomic>
#include <uuid.h>
#include <spdlog/spdlog.h>

#include "IOManager.hpp"
#include "SessionManager.hpp"
#include "Room.hpp"
#include "Session.hpp"
#include "World.hpp"
#include "Packet.pb.h"
#include "IngamePacketPool.hpp"

int RunWorldUpdateTest()
{
    // Set log level to info to prevent log flooding during the 30-second test
    spdlog::set_level(spdlog::level::info);
    
    constexpr std::uint32_t testTime = 10;
    constexpr std::uint32_t clientCount = 10;
    const auto threadCount = std::thread::hardware_concurrency();
    
    spdlog::info("===============TEST MODE================");
    spdlog::info("Starting {}-Second Load Test ({} Clients)...", testTime, clientCount);
    spdlog::info("========================================");
    
    IngamePacketPool::Init(100);

    auto ioManager = IOManager::Create("TestIOManager", threadCount, 4);
    auto sessionManager = SessionManager::Create();
    
    auto roomId = uuids::uuid_system_generator{}();
    auto room = Room::Create(ioManager, sessionManager, roomId);
    
    // Register a dummy callback to prevent bad_function_call when room becomes empty
    room->SetRemoveRoomCallback([](const std::shared_ptr<Room>&) {});

    std::vector<uuids::uuid> clientIds;
    std::vector<std::shared_ptr<Session>> sessions;

    for (int i = 0; i < clientCount; ++i)
    {
        auto sessionId = uuids::uuid_system_generator{}();
        clientIds.push_back(sessionId);

        auto session = Session::Create(ioManager, std::weak_ptr<Listener>{}, sessionId, 10000 + i);
        // Register a dummy UDP send handler so EnqueueUdpSendPacket doesn't Stop the session
        // MODIFIED: Used auto for data parameter to avoid referencing Session's private Raw type
        session->SetSendToHandler([](asio::ip::udp::endpoint ep, auto data) {
            // Dummy sender: do nothing
        });

        sessions.push_back(session);
        room->AddSession(sessionId, session);
    }

    // Initialize the World (starts the 50ms tick loop)
    room->WorldInit();

    World* world = room->GetWorld();
    assert(world != nullptr);

    spdlog::info("Spawning 10 simulated client threads (sending inputs every 16ms)...");

    std::atomic<bool> runClients{true};
    std::vector<std::thread> clientThreads;

    for (int i = 0; i < clientCount; ++i)
    {
        auto sessionId = clientIds[i];
        clientThreads.emplace_back([&runClients, room, sessionId, roomId, i, clientCount, &clientIds]() {
            while (runClients)
            {
                static thread_local int loopCount = 0;
                loopCount++;

                auto ingamePacket = IngamePacketPool::GetInstance()->Rent();
                ingamePacket->set_sessionid(uuids::to_string(sessionId));
                ingamePacket->set_roomid(uuids::to_string(roomId));

                if (loopCount % 50 == 0)
                {
                    ingamePacket->set_method(Protocol::IngameType::Shoot);
                    
                    // Aim at the next player in the list
                    uuids::uuid targetId = clientIds[(i + 1) % clientCount];
                    Vector3 targetPos;
                    Vector3 shooterPos;
                    Vector3 direction(1.0f, 0.0f, 0.0f);
                    
                    if (room->GetWorld()->GetPlayerPosition(targetId, targetPos) &&
                        room->GetWorld()->GetPlayerPosition(sessionId, shooterPos))
                    {
                        direction = Vector3(targetPos.x - shooterPos.x,
                                            targetPos.y - shooterPos.y,
                                            targetPos.z - shooterPos.z);
                    }
                    
                    std::string shootData(sizeof(Vector3), '\0');
                    std::memcpy(&shootData[0], &direction, sizeof(Vector3));
                    ingamePacket->set_data(shootData);

                    // Simulate 6 ticks RTT lag (approx 100ms)
                    std::size_t curServerTick = room->GetWorld()->GetTickCount();
                    std::size_t targetTick = (curServerTick > 6) ? (curServerTick - 6) : 0;
                    ingamePacket->set_clienttick(targetTick);
                }
                else
                {
                    ingamePacket->set_method(Protocol::IngameType::Move);

                    // Strafe back and forth along the Z axis
                    Vector3 shooterPos(0.0f, 0.0f, 0.0f);
                    room->GetWorld()->GetPlayerPosition(sessionId, shooterPos);

                    float zDir = std::sin(static_cast<float>(loopCount) * 0.05f);
                    Vector3 direction(0.0f, 0.0f, zDir);
                    float speed = 2.0f;
                    Vector3 velocity = direction * speed;

                    Protocol::MovePacket movePacket;
                    movePacket.set_playerid(uuids::to_string(sessionId));
                    movePacket.set_originx(shooterPos.x);
                    movePacket.set_originy(shooterPos.y);
                    movePacket.set_originz(shooterPos.z);
                    movePacket.set_dirx(velocity.x);
                    movePacket.set_diry(velocity.y);
                    movePacket.set_dirz(velocity.z);

                    std::string serializedMove;
                    if(movePacket.SerializeToString(&serializedMove))
                    {
                        ingamePacket->set_data(serializedMove);
                    }
                }

                // 수정 부분: 패킷 직렬화 및 역직렬화 전 과정 테스트 시뮬레이션
                // 1. 클라이언트: IngamePacket -> string 직렬화
                std::string ingameData;
                if (!ingamePacket->SerializeToString(&ingameData))
                {
                    spdlog::error("TestWorld: IngamePacket Serialize failed");
                    continue;
                }

                // 2. 클라이언트: 공용 Packet -> string 직렬화 (UDP 전송 포맷)
                Protocol::Packet sendPacket;
                sendPacket.set_type(Protocol::PacketType::Ingame);
                sendPacket.set_data(ingameData);

                std::string packetStream;
                if (!sendPacket.SerializeToString(&packetStream))
                {
                    spdlog::error("TestWorld: Packet Serialize failed");
                    continue;
                }

                // 3. 서버: 수신 데이터 역직렬화 (ProcessPacket 재현)
                Protocol::Packet recvPacket;
                if (recvPacket.ParseFromString(packetStream))
                {
                    if (recvPacket.type() == Protocol::PacketType::Ingame)
                    {
                        // 역직렬화할 타겟 패킷을 풀에서 대여
                        auto recvIngamePacket = IngamePacketPool::GetInstance()->Rent();
                        if (recvIngamePacket->ParseFromString(recvPacket.data()))
                        {
                            room->EnqueuePacket(recvIngamePacket);
                        }
                        else
                        {
                            spdlog::error("TestWorld: IngamePacket Deserialization failed");
                        }
                    }
                }
                else
                {
                    spdlog::error("TestWorld: Packet Deserialization failed");
                }

                std::this_thread::sleep_for(std::chrono::microseconds(16666)); // ~60FPS client updates
            }
        });
    }

    spdlog::info("Simulation running for {} seconds...", testTime);
    std::this_thread::sleep_for(std::chrono::seconds(testTime));
    spdlog::info("Simulation finished. Stopping threads and collecting stats...");

    // Stop client threads
    runClients = false;
    for (auto& t : clientThreads)
    {
        if (t.joinable())
        {
            t.join();
        }
    }

    // Stop room and sessions
    room->Stop();
    for (auto& session : sessions)
    {
        session->Stop();
    }
    ioManager->Stop();

    world->PrintScoreboard();

    IngamePacketPool::Release();
    return 0;
}
