#include <iostream>
#include <thread>

#include "IOManager.hpp"
#include "Matching.hpp"
#include "Server.hpp"
#include "SessionManager.hpp"

// Test Server Port
constexpr std::uint16_t SERVER_PORT = 52800;

int main()
{
    spdlog::info("type 'quit' to stop server");
    auto threadCount = std::thread::hardware_concurrency() * 2;
    auto blockingThreadCount = std::thread::hardware_concurrency() * 2;
    auto ioManager = IOManager::Create("first manager", threadCount, blockingThreadCount);

    auto sessionManager = SessionManager::Create();
    auto matching = Matching::Create(ioManager, sessionManager);
    auto server = Server::Create(ioManager, sessionManager, matching, SERVER_PORT);

    server->Start();

    std::string tmp;
    while(std::cin >> tmp)
    {
        if(tmp == "quit")
            break;
    }

    server->Stop();
    sessionManager->Clear();
    ioManager->Stop();
    return 0;
}
