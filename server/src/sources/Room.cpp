#include "Room.hpp"

#include "IOManager.hpp"
#include "SessionManager.hpp"
#include "Session.hpp"

Room::Room(SecretKey, std::shared_ptr<IOManager> ioManager, std::shared_ptr<SessionManager> sessionManager, uuids::uuid roomId)
: _ioManager(ioManager), _sessionManager(sessionManager), _roomId(roomId), _world(std::make_unique<World>(ioManager->GetIoContext(), roomId))
{
}

Room::~Room()
{
    spdlog::info("room {} destroyed", uuids::to_string(_roomId));
}

void Room::WorldInit()
{
    std::lock_guard<std::mutex> sessionsLock(_sessionsMutex);
    if(_sessions.empty())
    {
        spdlog::info("room {} invalid situation: no session", uuids::to_string(_roomId));
    }

    _world->Init(_sessions);
    spdlog::info("room {}: world create complete", uuids::to_string(_roomId));

    _world->StartUpdate(weak_from_this());
}

void Room::Stop()
{
    if(_removeRoomFromMatchingHandler != nullptr)
        _removeRoomFromMatchingHandler(shared_from_this());

    _removeRoomFromMatchingHandler = nullptr;

    _world->StopUpdate();

    if(!_sessions.empty())
    {
        _sessions.clear();
    }
}

void Room::AddSession(uuids::uuid sessionId, std::weak_ptr<Session> weakSession)
{
    std::lock_guard<std::mutex> lock(_sessionsMutex);
    if(auto session = weakSession.lock())
    {
        _sessions.insert({ sessionId, weakSession });
        session->AddDisconnectCallback([weakSelf = weak_from_this()](const std::weak_ptr<Session>& removeSession) {
            if(auto self = weakSelf.lock())
                self->RemoveSession(removeSession);
        });
    }
}

void Room::RemoveSession(std::weak_ptr<Session> weakRemoveSession)
{
    std::lock_guard<std::mutex> lock(_sessionsMutex);
    if(auto removeSession = weakRemoveSession.lock())
    {
        // 수정 부분: 삭제된 세션이 없으면(이미 정리되었거나 존재하지 않는 세션이면) 조기 리턴
        if (_sessions.erase(removeSession->GetId()) == 0)
        {
            return;
        }

        spdlog::info("room {}: remove session {}", uuids::to_string(_roomId), uuids::to_string(removeSession->GetId()));

        if(!_sessions.empty())
            return;

        spdlog::info("room: room {} is empty", uuids::to_string(_roomId));
        // MODIFIED: Added null check for safety when handler is not registered (e.g., during tests)
        if (_removeRoomFromMatchingHandler)
        {
            _removeRoomFromMatchingHandler(shared_from_this());
        }
    }
}

void Room::Broadcast(std::shared_ptr<Packet> packet)
{
    for(auto& [id, weakSession] : _sessions)
    {
        if(auto session = weakSession.lock())
        {
            if(!session->IsValid())
                continue;
            session->EnqueueUdpSendPacket(packet);
            spdlog::info("room: session {} send", uuids::to_string(session->GetId()));
        }
    }
}

void Room::EnqueuePacket(std::shared_ptr<IngamePacket> packet)
{
    _world->EnqueuePacket(packet);
}

void Room::SetRemoveRoomCallback(RemoveRoomCallback handler)
{
    _removeRoomFromMatchingHandler = std::move(handler);
}