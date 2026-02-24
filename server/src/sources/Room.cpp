#include "Room.hpp"
#include "Session.hpp"

void Room::AddSession(uuids::uuid sessionId, std::shared_ptr<Session> session)
{
    std::lock_guard<std::mutex> lock(_sessionsMutex);
    _sessions.insert({ sessionId, session });
}

void Room::RemoveSession(uuids::uuid sessionId)
{
    std::lock_guard<std::mutex> lock(_sessionsMutex);
    _sessions.erase(sessionId);
}

void Room::Broadcast()
{
    for(auto& [id, session] : _sessions)
    {
        spdlog::info("session {} send", uuids::to_string(session->GetId()));
    }
}
