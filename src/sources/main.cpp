#include <iostream>
#include <thread>

#include "IOManager.h"
#include "Server.h"

// Test port
constexpr std::uint16_t SERVER_PORT = 52800;

int main()
{
    auto threadCount = std::thread::hardware_concurrency() * 2;
    auto ioManager = IOManager::Create("first manager", threadCount);
    auto server = Server::Create(ioManager, SERVER_PORT);

    server->Start();
    server->Test();

    std::string tmp;
    std::cin >> tmp;

    ioManager->Stop();
    return 0;
}
