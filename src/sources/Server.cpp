#include "Server.hpp"

#include "IOManager.hpp"
#include "Room.hpp"
#include "Session.hpp"

Server::Server(SecretKey, std::shared_ptr<IOManager> ioManager, std::uint16_t port)
: _ioManager(ioManager), _serverEndpoint(asio::ip::tcp::v4(), port), _acceptor(ioManager->GetIoContext())
{
    spdlog::info("Server Object Created");
}

void Server::Test() {}

void Server::Start()
{
    spdlog::info("server Started...");
    AcceptAsync();
}

void Server::Stop() { spdlog::warn("server Stoped...\n"); }

void Server::AcceptAsync()
{
    auto self(shared_from_this());
    auto newSession = Session::Create(_ioManager->GetIoContext());
    _acceptor.async_accept(*newSession->GetSocket(), [self, newSession](std::error_code ec) {
        if(ec)
        {
            spdlog::error("accept error occured: {}", ec.message());
            return;
        }

        std::string_view sessionAddr = newSession->GetEndpoint().address().to_string();
        std::string_view roomId = newSession->GetRoomId();

        if(self->_rooms.find(roomId.data()) == self->_rooms.end())
        {
            spdlog::error("invalid room id: request {}, room id {}", sessionAddr, roomId);
            return;
        }

        self->_rooms[roomId.data()]->AddSession(newSession->GetId(), newSession);
        spdlog::info("new session allocated to room {}", roomId);
    });
}