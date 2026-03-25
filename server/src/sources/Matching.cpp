#include "Matching.hpp"
#include "Server.hpp"
#include "Session.hpp"
void Matching::AddWaitSession(uuids::uuid waitSessionId, std::shared_ptr<Session> session)
{
    {
        std::lock_guard<std::mutex> queueLock(_waitingQueueMutex);
        _waitingQueue.push_back({ waitSessionId, session });
        spdlog::info("matching: waiting queue is added {}", uuids::to_string(waitSessionId));
    }

    session->SetNotifyDisconnectCallback([weakSelf = weak_from_this()](const std::shared_ptr<Session>& removeSession) {
        if(auto self = weakSelf.lock())
            self->RemoveSession(removeSession);
    });

    TryMatch();
}

void Matching::Start()
{
    _isRunning = true;
    spdlog::info("matching: matching started");
}

void Matching::Stop()
{
    spdlog::info("matching: matching stopped");
    std::lock_guard<std::mutex> queueLock(_waitingQueueMutex);
    _isRunning = false;

    while(!_waitingQueue.empty())
    {
        auto [id, session] = _waitingQueue.front();
        session->Stop();

        _waitingQueue.pop_front();
    }
    if(!_activeRooms.empty())
    {
        for(const auto& [roomId, room] : _activeRooms)
        {
            _removeRoomFromServerHandler(room);
        }
    }

    _activeRooms.clear();

    _registerRoomToServerHandler = nullptr;
    _removeRoomFromServerHandler = nullptr;
}

void Matching::TryMatch()
{
    std::lock_guard<std::mutex> queueLock(_waitingQueueMutex);

    if(!_isRunning)
        return;

    // Check if we have enough players to match
    while(_waitingQueue.size() >= MATCHING_PLAYERS)
    {
        // Matching Sequence (match 10 sessions at the front)
        auto newRoom = Room::Create(_ioManager, _uuidGen());
        newRoom->SetRemoveRoomCallback([weakSelf = weak_from_this()](const std::shared_ptr<Room>& removeRoom) {
            if(auto self = weakSelf.lock())
                self->RemoveRoom(removeRoom);
        });

        for(auto cnt = 0; cnt < MATCHING_PLAYERS; ++cnt)
        {
            auto [nextSessionId, nextSession] = _waitingQueue.front();
            newRoom->AddSession(nextSessionId, nextSession);
            nextSession->SetRoomAndSendInfo(newRoom->GetId());

            _waitingQueue.pop_front();
        }

        newRoom->WorldInit();

        std::lock_guard<std::mutex> roomsLock(_activeRoomsMutex);
        _activeRooms.push_back({ newRoom->GetId(), newRoom });
        _registerRoomToServerHandler(newRoom);

        spdlog::info("matching: new matching complete room {}, active room ({})", uuids::to_string(newRoom->GetId()), _activeRooms.size());
    }
}

void Matching::SetRegisterRoomCallback(RoomCallback handler)
{
    _registerRoomToServerHandler = std::move(handler);
}

void Matching::SetRemoveRoomCallback(RoomCallback handler)
{
    _removeRoomFromServerHandler = std::move(handler);
}

void Matching::RemoveSession(std::shared_ptr<Session> removeSession)
{
    std::lock_guard<std::mutex> waitingQueueLock(_waitingQueueMutex);

    auto removeId = removeSession->GetId();
    auto it = std::find_if(_waitingQueue.begin(), _waitingQueue.end(), [removeId](const auto& p) { return p.first == removeId; });

    if(it == _waitingQueue.end())
        return;

    _waitingQueue.erase(it);
    spdlog::info("matching: removed session {} from waiting queue", uuids::to_string(removeId));
}

void Matching::RemoveRoom(std::shared_ptr<Room> removeRoom)
{
    std::lock_guard<std::mutex> activeRoomLock(_activeRoomsMutex);
    auto removeId = removeRoom->GetId();
    auto it = std::find_if(_activeRooms.begin(), _activeRooms.end(), [removeId](const auto& p) { return p.first == removeId; });

    if(it == _activeRooms.end())
    {
        spdlog::error("matching: no room {} in active rooms", uuids::to_string(removeId));
        return;
    }

    _activeRooms.erase(it);
    _removeRoomFromServerHandler(removeRoom);
    spdlog::info("matching: removed room {} from active rooms, active rooms count ({})", uuids::to_string(removeId), _activeRooms.size());
}