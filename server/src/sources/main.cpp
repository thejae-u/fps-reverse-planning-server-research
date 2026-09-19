#include <iostream>
#include <thread>
#include <vector>

#include "Base.hpp"
#include "IOManager.hpp"
#include "Listener.hpp"
#include "PacketPool.hpp"
#include "Room.hpp"
#include "TestMode.h"

int main(const int argc, char** argv)
{
    // 실행인자 파싱 및 검증
    if(argc == 1)
    {
        spdlog::error("no options");
        exit(0);
    }
    
    if(!std::strcmp(argv[1], "--test"))
    {
        std::uint16_t testClientCount = 10;
        if(argc > 2)
        {
            try
            {
                testClientCount = std::stoi(argv[2]);
            }
            catch(const std::exception& e)
            {
                spdlog::error(e.what());
                exit(0);
            }
        }
            
        spdlog::info("running test");
        const auto testMode = std::make_unique<TestMode>(testClientCount);
        testMode->RunTestMode();
        exit(0);
    }

    const ServerConfig config = ServerConfig::Parse(argc, argv);
    if(config.matchId.empty())
    {
        spdlog::error("invalid match id");
        exit(0);
    }
    
    if(config.apiKey.empty())
    {
        spdlog::error("invalid api key");
        exit(0);
    }
    
    if(config.tcpPort == 0 || config.udpPort == 0)
    {
        spdlog::error("invalid port");
        exit(0);
    }
    
    if(config.allowedPlayers.size() == 0)
    {
        spdlog::error("no players specified");
        exit(0);
    }
    
    // 서버 시작
    spdlog::info("type 'quit' to stop server");
    const auto threadCount = std::thread::hardware_concurrency();
    constexpr auto blockingThreadCount = 4;
    const auto ioManager = IOManager::Create("I/O Manager", threadCount, blockingThreadCount);
    
    if(!ioManager)
        throw std::runtime_error("failed to create io manager");
    
    constexpr auto ingamePacketPoolSize = 100;
    constexpr auto networkPacketPoolSize = 100;
    constexpr auto byteBufferPoolSize = 100;

    IngamePacketPool::Init(ingamePacketPoolSize);
    NetworkPacketPool::Init(networkPacketPoolSize);
    ByteBufferPool::Init(byteBufferPoolSize);
    
    // broadcast용 room
    auto matchId = uuids::uuid::from_string(config.matchId).value_or(uuids::uuid_system_generator{}());
    const auto dedicatedRoom = Room::Create(ioManager, matchId, config.apiKey, config.allowedPlayers.size());
    const auto listener = Listener::Create(ioManager, config.tcpPort, config.udpPort, config.allowedPlayers);
    listener->SetDedicatedRoom(dedicatedRoom);

    // Start, Stop을 처리하기 위한 컨테이너
    std::vector<std::shared_ptr<IBase>> components;
    components.emplace_back(listener);

    for(const auto& component : components)
        component->Start();

    std::string tmp;
    while(std::cin >> tmp)
    {
        if(tmp == "quit")
            break;
    }

    for(auto it = components.rbegin(); it != components.rend(); ++it)
        (*it)->Stop();

    ioManager->Stop();
    
    IngamePacketPool::Release();
    NetworkPacketPool::Release();
    ByteBufferPool::Release();

    return 0;
}
