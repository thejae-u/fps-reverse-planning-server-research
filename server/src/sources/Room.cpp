#include "Room.hpp"

#include "IOManager.hpp"
#include "SessionManager.hpp"
#include "Session.hpp"

void Room::WorldInit()
{
    std::lock_guard<std::mutex> sessionsLock(_sessionsMutex);
    if(_sessions.empty())
    {
        spdlog::info("room {} invalid situation: no session", uuids::to_string(_roomId));
    }

    std::vector<uuids::uuid> sessionIds(_sessions.size());
    for(const auto& [id, session] : _sessions)
    {
        sessionIds.emplace_back(id);
    }

    _world->Init(sessionIds);

    spdlog::info("room {}: world create complete", uuids::to_string(_roomId));

    _ioManager->PostOnBlockingPool([weakSelf = weak_from_this()]() {
        if(auto self = weakSelf.lock())
            self->DequeuePacketAsync();
    });
}

void Room::Stop()
{
    if(_removeRoomFromMatchingHandler != nullptr)
        _removeRoomFromMatchingHandler(shared_from_this());

    _removeRoomFromMatchingHandler = nullptr;

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
        session->AddDisconnectListener([weakSelf = weak_from_this()](const std::weak_ptr<Session>& removeSession) {
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
        spdlog::info("room: remove session {}", uuids::to_string(_roomId), uuids::to_string(removeSession->GetId()));
        _sessions.erase(removeSession->GetId());

        if(!_sessions.empty())
            return;

        spdlog::info("room: room {} is empty", uuids::to_string(_roomId));
        _removeRoomFromMatchingHandler(shared_from_this());
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
    std::lock_guard<std::mutex> packetQueueLock(_packetQueueMutex);
    _sendPacketQueue.push(packet);

    _packetQueueCv.notify_one();
}

void Room::DequeuePacketAsync()
{
    std::unique_lock<std::mutex> packetQueueLock(_packetQueueMutex);
    _packetQueueCv.wait(packetQueueLock, [&] {
        return !_sendPacketQueue.empty();
    });

    if(!_isRunning)
        return;


    auto packet = _sendPacketQueue.front();
    _sendPacketQueue.pop();

    // valid packet logic (todo)

    std::size_t packetSize = packet->ByteSizeLong();
    std::string sendBuffer;
    if(packet->SerializeToString(&sendBuffer))
    {
        auto sendPacket = std::make_shared<Packet>();
        sendPacket->set_type(PacketType::Ingame);
        sendPacket->set_data(sendBuffer);
        Broadcast(std::move(sendPacket));
    }
    else
    {
        spdlog::error("room {}: serialize error", uuids::to_string(_roomId));
    }

    _ioManager->PostOnBlockingPool([weakSelf = weak_from_this()]() {
        if(auto self = weakSelf.lock())
            self->DequeuePacketAsync();
    });
}

void Room::SetRemoveRoomCallback(RemoveRoomCallback handler)
{
    _removeRoomFromMatchingHandler = std::move(handler);
}