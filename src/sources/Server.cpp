#include "Server.h"

#include "IOManager.h"

Server::Server(SecretKey, std::shared_ptr<IOManager> ioManager, std::uint16_t port)
    : _ioManager(ioManager),
      _serverEndpoint(asio::ip::tcp::v4(), port), _acceptor(ioManager->GetIoContext())
{
    spdlog::info("Server Object Created");
}

void Server::Test()
{
    if (auto sharedIO = _ioManager.lock())
    {
        sharedIO->RegisterWork(
            []()
            { spdlog::info("Called from Server Test()"); });
    }
}

void Server::Start()
{
    spdlog::info("Server Started...");
    AcceptAsync();
}

void Server::Stop()
{
    spdlog::warn("Server Stoped...\n");
}

void Server::AcceptAsync()
{
    spdlog::info("Waiting for Clients...");
}