#include "SessionManager.hpp"

#include "Session.hpp"

void SessionManager::Clear()
{
    std::lock_guard<std::mutex> sessionsLock(_sessionsMutex);
    for(auto& [id, session] : _sessions)
    {
        if(session->IsValid())
            session->Stop();
    }

    _sessions.clear();
}

std::weak_ptr<Session> SessionManager::Find(uuids::uuid id)
{
    std::lock_guard<std::mutex> sessionsLock(_sessionsMutex);
    if(auto it = _sessions.find(id); it != _sessions.end())
        return it->second;

    return std::weak_ptr<Session>{};
}

std::weak_ptr<Session> SessionManager::Insert(uuids::uuid id, const std::shared_ptr<Session>& session)
{
    std::lock_guard<std::mutex> sessionsLock(_sessionsMutex);
    if(auto session = _sessions.find(id); session != _sessions.end())
    {
        spdlog::error("session manager: already exist session id {}", uuids::to_string(id));
        return std::weak_ptr<Session>{};
    }

    session->AddDisconnectCallback([weakSelf = weak_from_this()](const std::shared_ptr<Session>& delSession) {
        if(auto self = weakSelf.lock())
        {
            self->Erase(delSession->GetId());
        }
    });

    _sessions[id] = session;
    spdlog::info("session manager: session {} insert success", uuids::to_string(id));

    return _sessions[id];
}

void SessionManager::Erase(uuids::uuid id)
{
    std::lock_guard<std::mutex> sessionsLock(_sessionsMutex);
    if(auto it = _sessions.find(id); it == _sessions.end())
    {
        spdlog::error("session manager: no session {}", uuids::to_string(id));
        return;
    }

    _sessions.erase(id);
    spdlog::info("session {} erased from session manager", uuids::to_string(id));
}

std::size_t SessionManager::Size()
{
    std::lock_guard<std::mutex> sessionsLock(_sessionsMutex);
    return _sessions.size();
}