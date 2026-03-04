#include "Matching.hpp"
#include "Server.hpp"
#include "Session.hpp"

void Matching::AddWaitSession(uuids::uuid waitSessionId, std::shared_ptr<Session> session)
{
    std::lock_guard<std::mutex> queueLock(_waitingQueueMutex);
    _waitingQueue.push_back({ waitSessionId, session });
    spdlog::info("waiting queue is added {}", uuids::to_string(waitSessionId));

    session->SetNotifyDisconnectCallback([weakSelf = weak_from_this()](const std::shared_ptr<Session>& removeSession) {
        if(auto self = weakSelf.lock())
            self->RemoveSession(removeSession);
    });

    _waitingCv.notify_one();
}

void Matching::Start()
{
    _isRunning = true;

    _ioManager->RegisterWork([weakSelf = weak_from_this()]() {
        if(auto self = weakSelf.lock())
            self->MatchMaking();
    });
}

void Matching::Stop()
{
    std::lock_guard<std::mutex> queueLock(_waitingQueueMutex);
    _isRunning = false;

    _waitingCv.notify_one();

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

    spdlog::info("matching stopped");
}

void Matching::MatchMaking()
{
    std::unique_lock<std::mutex> queueLock(_waitingQueueMutex);
    spdlog::info("match making waiting...");

    // waiting for matching player
    _waitingCv.wait(queueLock, [weakSelf = weak_from_this()]() -> bool {
        if(auto self = weakSelf.lock())
            return self->_waitingQueue.size() >= MATCHING_PLAYERS || !self->_isRunning;
        return true;
    });

    if(!_isRunning)
    {
        spdlog::info("server is off cancel matchmaking");
        return;
    }

    // Matching Sequence (match 10 sessions at the front)
    auto newRoom = Room::Create(_uuidGen());
    newRoom->SetRemoveRoomCallback([weakSelf = weak_from_this()](const std::shared_ptr<Room>& removeRoom) {
        if(auto self = weakSelf.lock())
            self->RemoveRoom(removeRoom);
    });

    for(auto cnt = 0; cnt < MATCHING_PLAYERS; ++cnt)
    {
        auto [nextSessionId, nextSession] = _waitingQueue.front();
        newRoom->AddSession(nextSessionId, nextSession);
        nextSession->SetRoom(newRoom->GetId());

        _waitingQueue.pop_front();
    }

    std::lock_guard<std::mutex> roomsLock(_activeRoomsMutex);
    _activeRooms.push_back({ newRoom->GetId(), newRoom });
    _registerRoomToServerHandler(newRoom);

    auto weakSelf(weak_from_this());

    // Matching again
    if(auto self = weakSelf.lock())
    {
        // register MatchMaking function (lambda)
        self->_ioManager->RegisterWork([weakSelf]() {
            if(auto self = weakSelf.lock())
            {
                self->MatchMaking();
            }
        });
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
    spdlog::info("removed session {} from waiting queue", uuids::to_string(removeId));

    _waitingCv.notify_one();
}

void Matching::RemoveRoom(std::shared_ptr<Room> removeRoom)
{
    std::lock_guard<std::mutex> activeRoomLock(_activeRoomsMutex);
    auto removeId = removeRoom->GetId();
    auto it = std::find_if(_activeRooms.begin(), _activeRooms.end(), [removeId](const auto& p) { return p.first == removeId; });

    if(it == _activeRooms.end())
    {
        spdlog::error("invalid room id: no room {} in active rooms", uuids::to_string(removeId));
        return;
    }

    _activeRooms.erase(it);
    _removeRoomFromServerHandler(removeRoom);
    spdlog::info("removed room {} from active rooms", uuids::to_string(removeId));
}