#include "Server.hpp"

#include "IOManager.hpp"
#include "Room.hpp"
#include "Session.hpp"
#include "Matching.hpp"

Server::Server(SecretKey, std::shared_ptr<IOManager> ioManager, std::shared_ptr<Matching> matching, std::uint16_t port)
: _ioManager(ioManager), _matching(matching), _serverEp(asio::ip::tcp::v4(), port), _acceptor(ioManager->GetIoContext(), _serverEp)
{
    spdlog::info("server object created");
}

void Server::Start()
{
    spdlog::info("server started...");
    AcceptAsync();
}

void Server::Stop() 
{ 
    _acceptor.cancel();
    spdlog::info("server stoped...\n"); 
}

void Server::AcceptAsync()
{
    auto weakSelf(weak_from_this());
    auto newSession = Session::Create(_ioManager->GetIoContext(), _uuidGen());
    _acceptor.async_accept(*newSession->GetSocket(), [weakSelf, newSession](std::error_code ec) {
        if(ec)
        {
            if (ec == asio::error::connection_aborted ||
                ec == asio::error::operation_aborted)
            {
                spdlog::warn("acceptor aborted");
                return;
            }

            spdlog::error("accept error occured: {}", ec.message());
            return;
        }

        auto sessionAddrStr = newSession->GetEndpoint().address().to_string();
        auto sessionId = newSession->GetId();

        if(auto self = weakSelf.lock())
        {
            std::lock_guard<std::mutex> sessionsLock(self->_sessionsMutex);
            if(self->_sessions.find(sessionId) != self->_sessions.end())
            {
                spdlog::error("invalid session id (session is already exsist): {}", uuids::to_string(sessionId));
                self->AcceptAsync();
                return;
            }

            // push to match-making waiting queue
            // move session ownership to Matching
            self->_matching->AddWaitSession(sessionId, newSession);
            newSession->Start();

            // new session create for accept other client
            self->AcceptAsync();
        }
    });
}