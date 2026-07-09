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
    if(const auto session = weakSession.lock())
    {
        _sessions.insert({ sessionId, weakSession });
        session->AddDisconnectCallback([weakSelf = weak_from_this()](const std::weak_ptr<Session>& removeSession) {
            if(const auto self = weakSelf.lock())
                self->RemoveSession(removeSession);
        });
    }
}

void Room::RemoveSession(std::weak_ptr<Session> weakRemoveSession)
{
    std::lock_guard<std::mutex> lock(_sessionsMutex);
    if(const auto removeSession = weakRemoveSession.lock())
    {
        if (_sessions.erase(removeSession->GetId()) == 0)
        {
            return;
        }

        spdlog::info("room {}: remove session {}", uuids::to_string(_roomId), uuids::to_string(removeSession->GetId()));

        if(!_sessions.empty())
            return;

        spdlog::info("room: room {} is empty", uuids::to_string(_roomId));
        if (_removeRoomFromMatchingHandler)
        {
            _removeRoomFromMatchingHandler(shared_from_this());
        }
    }
}

void Room::Broadcast(std::shared_ptr<Packet> packet)
{
    for(const auto& [id, weakSession] : _sessions)
    {
        if(const auto session = weakSession.lock())
        {
            if(!session->IsValid())
                continue;
            session->EnqueueUdpSendPacket(packet);
        }
    }
}

void Room::EnqueuePacket(std::shared_ptr<IngamePacket> packet) const
{
    _world->EnqueuePacket(packet);
}

void Room::SetRemoveRoomCallback(RemoveRoomCallback handler)
{
    _removeRoomFromMatchingHandler = std::move(handler);
}