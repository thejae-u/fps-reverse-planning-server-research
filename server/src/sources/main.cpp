#include <iostream>
#include <thread>
#include <vector>

#include "Base.hpp"
#include "IOManager.hpp"
#include "Matching.hpp"
#include "Listener.hpp"
#include "SessionManager.hpp"
#include "ConnectionPool.hpp"
#include "IngamePacketPool.hpp"
#include "TestWorld.hpp"

// Test Server Port
constexpr std::uint16_t SERVER_PORT = 52800;

int main(int argc, char* argv[])
{
    if (argc > 1 && std::string(argv[1]) == "--test")
    {
        return RunWorldUpdateTest();
    }

    spdlog::info("type 'quit' to stop server");
    const auto threadCount = std::thread::hardware_concurrency();
    const auto blockingThreadCount = 4;
    const auto ioManager = IOManager::Create("first manager", threadCount, blockingThreadCount);
    
    if(!ioManager)
        throw std::runtime_error("failed to create io manager");
    
    constexpr auto ingamePacketPoolSize = 100;
    IngamePacketPool::Init(ingamePacketPoolSize);
    
    constexpr auto internalPoolSize = 5;
    const auto internalConnectionPool = ConnectionPool::Create(ioManager, internalPoolSize);
    
    const auto sessionManager = SessionManager::Create();
    const auto matching = Matching::Create(ioManager, sessionManager);
    const auto listener = Listener::Create(ioManager, sessionManager, matching, SERVER_PORT);

    // Start, Stop을 처리하기 위한 컨테이너
    std::vector<std::shared_ptr<IBase>> components;
    components.emplace_back(internalConnectionPool);
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

    sessionManager->Clear();
    ioManager->Stop();
    
    IngamePacketPool::Release();

    return 0;
}
