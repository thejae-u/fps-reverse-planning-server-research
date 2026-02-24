#include <iostream>
#include <thread>

#include "IOManager.hpp"
#include "Matching.hpp"
#include "Server.hpp"

// Test Server Port
constexpr std::uint16_t SERVER_PORT = 52800;

int main()
{
    auto threadCount = std::thread::hardware_concurrency() * 2;
    auto ioManager = IOManager::Create("first manager", threadCount);
    auto matching = Matching::Create(ioManager);
    auto server = Server::Create(ioManager, matching, SERVER_PORT);

    matching->Start();
    server->Start();
    server->Test();

    ioManager->RegisterWork([matching]() { matching->MatchMaking(); });

    std::string tmp;
    std::cin >> tmp;

    matching->Stop();
    server->Stop();
    ioManager->Stop();
    return 0;
}
