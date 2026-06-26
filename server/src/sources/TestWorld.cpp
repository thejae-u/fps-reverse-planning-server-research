#include "TestWorld.hpp"

#include <iostream>
#include <thread>
#include <cassert>
#include <chrono>
#include <vector>
#include <atomic>
#include <cstring>
#include <uuid.h>
#include <spdlog/spdlog.h>

#include "IOManager.hpp"
#include "SessionManager.hpp"
#include "Room.hpp"
#include "Session.hpp"
#include "World.hpp"
#include "Packet.pb.h"

int RunWorldUpdateTest()
{
    // Set log level to info to prevent log flooding during the 30-second test
    spdlog::set_level(spdlog::level::info);
    
    spdlog::info("=========================================");
    spdlog::info("Starting 30-Second Load Test (10 Clients)...");
    spdlog::info("=========================================");

    const int clientCount = 10;
    auto ioManager = IOManager::Create("TestIOManager", 4, 4);
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
    world->ClearMetrics();

    spdlog::info("Spawning 10 simulated client threads (sending inputs every 16ms)...");

    std::atomic<bool> runClients{true};
    std::vector<std::thread> clientThreads;

    for (int i = 0; i < clientCount; ++i)
    {
        auto sessionId = clientIds[i];
        clientThreads.emplace_back([&runClients, room, sessionId, roomId]() {
            while (runClients)
            {
                auto ingamePacket = std::make_shared<Protocol::IngamePacket>();
                ingamePacket->set_sessionid(uuids::to_string(sessionId));
                ingamePacket->set_roomid(uuids::to_string(roomId));
                ingamePacket->set_method(Protocol::IngameType::Move);

                // 수정 부분: 16바이트 MoveData 패킹 전송 (방향 Vector3 + 속도 int32_t)
                struct MoveData {
                    Vector3 direction{1.0f, 0.0f, 1.0f};
                    std::int32_t speed = 10;
                } data;
                std::string moveData(sizeof(MoveData), '\0');
                std::memcpy(&moveData[0], &data, sizeof(MoveData));
                ingamePacket->set_data(moveData);

                room->EnqueuePacket(ingamePacket);

                std::this_thread::sleep_for(std::chrono::microseconds(16666)); // ~60FPS client updates
            }
        });
    }

    spdlog::info("Simulation running for 30 seconds...");
    std::this_thread::sleep_for(std::chrono::seconds(30));
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

    // Query and print latency metrics
    std::int64_t minUs = 0;
    std::int64_t maxUs = 0;
    double avgUs = 0.0;
    world->GetMetrics(minUs, maxUs, avgUs);

    spdlog::info("=========================================");
    spdlog::info("Load Test Results (World Tick Update Duration):");
    spdlog::info("-----------------------------------------");
    spdlog::info("Min Delay: {:.3f} ms", static_cast<float>(minUs) / 1000.0);
    spdlog::info("Max Delay: {:.3f} ms", static_cast<float>(maxUs) / 1000.0);
    spdlog::info("Avg Delay: {:.3f} ms", static_cast<float>(avgUs) / 1000.0);
    spdlog::info("=========================================");

    return 0;
}
