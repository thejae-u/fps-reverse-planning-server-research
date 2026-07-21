#include "Room.hpp"

#include "IOManager.hpp"
#include "Session.hpp"

Room::Room(SecretKey, std::shared_ptr<IOManager> ioManager, uuids::uuid roomId, std::size_t expectedPlayerCount)
    : _ioManager(ioManager), _roomId(roomId), _expectedPlayerCount(expectedPlayerCount), _world(std::make_unique<World>(ioManager->GetIoContext(), roomId))
{
}

Room::~Room()
{
    spdlog::info("room: destroyed", uuids::to_string(_roomId));
}

void Room::TryStartGameNoLock()
{
    if(_isWorldStarted)
        return;

    if(_sessions.size() >= _expectedPlayerCount)
    {
        if(!_isWorldStarted.exchange(true))
        {
            spdlog::info("room: all players connected! starting world...", uuids::to_string(_roomId));
            WorldInitNoLock();
        }
    }
}

void Room::WorldInitNoLock()
{
    _world->Init(_sessions);
    spdlog::info("room: world create complete", uuids::to_string(_roomId));

    _world->StartUpdate(weak_from_this());
}

void Room::Stop()
{
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

    TryStartGameNoLock();
}

void Room::RemoveSession(std::weak_ptr<Session> weakRemoveSession)
{
    std::lock_guard<std::mutex> lock(_sessionsMutex);
    if(const auto removeSession = weakRemoveSession.lock())
    {
        if(_sessions.erase(removeSession->GetId()) == 0)
        {
            return;
        }

        spdlog::info("room: remove session {}", uuids::to_string(_roomId), uuids::to_string(removeSession->GetId()));

        if(!_sessions.empty())
            return;

        spdlog::info("room: all session removed", uuids::to_string(_roomId));
        _world->StopUpdate();
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