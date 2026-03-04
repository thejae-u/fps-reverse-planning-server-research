#include <iostream>
#include <thread>

#include "IOManager.hpp"
#include "Matching.hpp"
#include "Server.hpp"

// Test Server Port
constexpr std::uint16_t SERVER_PORT = 52800;

int main()
{
    spdlog::info("type 'quit' to stop server");
    auto threadCount = std::thread::hardware_concurrency() * 2;
    auto ioManager = IOManager::Create("first manager", threadCount);
    auto matching = Matching::Create(ioManager);
    auto server = Server::Create(ioManager, matching, SERVER_PORT);

    server->Start();

    std::string tmp;
    while(std::cin >> tmp)
    {
        if(tmp == "quit")
            break;
    }

    matching->Stop();
    server->Stop();
    ioManager->Stop();
    return 0;
}
