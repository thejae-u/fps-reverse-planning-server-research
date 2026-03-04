#include "Room.hpp"
#include "Session.hpp"

void Room::AddSession(uuids::uuid sessionId, std::shared_ptr<Session> session)
{
    std::lock_guard<std::mutex> lock(_sessionsMutex);
    _sessions.insert({ sessionId, session });
    session->SetNotifyDisconnectCallback([weakSelf = weak_from_this()](const std::shared_ptr<Session>& removeSession) {
        if(auto self = weakSelf.lock())
            self->RemoveSession(removeSession);
    });
}

void Room::RemoveSession(std::shared_ptr<Session> removeSession)
{
    std::lock_guard<std::mutex> lock(_sessionsMutex);
    spdlog::info("room {} remove session {}", uuids::to_string(_roomId), uuids::to_string(removeSession->GetId()));
    _sessions.erase(removeSession->GetId());
}

void Room::Broadcast()
{
    for(auto& [id, session] : _sessions)
    {
        spdlog::info("session {} send", uuids::to_string(session->GetId()));
    }
}
