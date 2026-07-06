#include "App.hpp"
#include "IOManager.hpp"
#include "NetworkClient.hpp"
#include <vector>
#include <string>
#include <thread>
#include <chrono>
#include <cstdlib>
#include <ctime>
#include <spdlog/spdlog.h>

int main(int argc, char* argv[]) {
    if (argc > 1 && std::string(argv[1]) == "--cli") {
        spdlog::info("Running in CLI mode for matchmaking test...");
        std::srand(static_cast<unsigned int>(std::time(nullptr)));
        
        auto ioManager = IOManager::Create("ClientIO", 4, 4);
        std::vector<std::shared_ptr<NetworkClient>> clients;
        
        for (int i = 0; i < 10; ++i) {
            auto client = std::make_shared<NetworkClient>(ioManager);
            client->Connect("127.0.0.1", 52800);
            clients.push_back(client);
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        
        // Wait 3 seconds for matchmaking to complete
        std::this_thread::sleep_for(std::chrono::seconds(3));
        
        // Send random traffic for 7 seconds
        spdlog::info("Sending random traffic from CLI clients...");
        for (int step = 0; step < 70; ++step) {
            for (auto& client : clients) {
                if (client->IsConnected() && client->IsIngame()) {
                    int packetType = std::rand() % 3; // 0: Move, 1: Jump, 2: Shoot
                    if (packetType == 0) {
                        struct MoveData {
                            float dx = 0.0f;
                            float dy = 0.0f;
                            float dz = 0.0f;
                            std::int32_t speed = 10;
                        } data;
                        data.dx = static_cast<float>(std::rand()) / static_cast<float>(RAND_MAX) * 2.0f - 1.0f;
                        data.dz = static_cast<float>(std::rand()) / static_cast<float>(RAND_MAX) * 2.0f - 1.0f;
                        data.speed = std::rand() % 15 + 5;
                        std::string sendData(sizeof(MoveData), '\0');
                        std::memcpy(&sendData[0], &data, sizeof(MoveData));
                        client->SendIngamePacket(IngameType::Move, sendData);
                    } else if (packetType == 1) {
                        client->SendIngamePacket(IngameType::Jump, "");
                    } else if (packetType == 2) {
                        struct ShootData {
                            float x = 0.0f;
                            float y = 0.0f;
                            float z = 1.0f;
                        } data;
                        data.x = static_cast<float>(std::rand()) / static_cast<float>(RAND_MAX) * 2.0f - 1.0f;
                        data.z = static_cast<float>(std::rand()) / static_cast<float>(RAND_MAX) * 2.0f - 1.0f;
                        std::string sendData(sizeof(ShootData), '\0');
                        std::memcpy(&sendData[0], &data, sizeof(ShootData));
                        client->SendIngamePacket(IngameType::Shoot, sendData, 0);
                    }
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        
        // Print final status
        spdlog::info("--- Client Matchmaking Results ---");
        int matchedCount = 0;
        for (int i = 0; i < 10; ++i) {
            auto& client = clients[i];
            bool connected = client->IsConnected();
            std::string roomId = client->GetRoomId();
            std::string sessionId = client->GetSessionId();
            
            if (!roomId.empty()) {
                matchedCount++;
                spdlog::info("Client [{}] - Status: MATCHED | Room: {} | Session: {}", i, roomId, sessionId);
            } else {
                spdlog::info("Client [{}] - Status: NOT MATCHED (Connected: {}) | Session: {}", i, connected ? "YES" : "NO", sessionId);
            }
        }
        spdlog::info("Total Matched: {}/10", matchedCount);
        
        // Disconnect all
        for (auto& client : clients) {
            client->Disconnect();
        }
        ioManager->Stop();
        return 0;
    }

    auto app = std::make_unique<App>();

    if (app->Init()) {
        app->Run();
    }

    return 0;
}
