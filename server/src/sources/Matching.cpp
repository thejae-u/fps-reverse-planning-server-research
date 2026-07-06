#include "Matching.hpp"

#include "SessionManager.hpp"
#include "Listener.hpp"
#include "Session.hpp"

bool Matching::AddWaitSession(uuids::uuid waitSessionId, std::weak_ptr<Session> weakSession, MatchingLastError& type)
{
    std::lock_guard<std::mutex> queueLock(_waitingQueueMutex);
    if(_weakSessions.find(waitSessionId) != _weakSessions.end())
    {
        spdlog::warn("matching: session {} already in waiting queue", uuids::to_string(waitSessionId));
        type = MatchingLastError::FailedByExsists;
        return false;
    }

    _waitingQueue.emplace_back(waitSessionId, weakSession);
    _weakSessions[waitSessionId] = weakSession;

    spdlog::info("matching: waiting queue is added {}", uuids::to_string(waitSessionId));

    if(auto session = weakSession.lock())
    {
        auto handle = session->AddDisconnectCallback([weakSelf = weak_from_this()](const std::weak_ptr<Session>& weakRemoveSession) {
            if(auto self = weakSelf.lock())
                self->RemoveSession(weakRemoveSession);
        });

        _sessionCallbackHandles[waitSessionId] = handle;

        _ioManager->PostOnBlockingPool([weakSelf = weak_from_this()]() {
            if(auto self = weakSelf.lock())
                self->TryMatch();
        });
    }

    type = MatchingLastError::Success;
    return true;
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
        auto [id, weakSession] = _waitingQueue.front();
        if(auto session = weakSession.lock())
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
    if(!_isRunning)
        return;

    // Check if we have enough players to match
    std::lock_guard<std::mutex> queueLock(_waitingQueueMutex);
    while(_waitingQueue.size() >= MATCHING_PLAYERS)
    {
        // Matching Sequence (match 10 sessions at the front)
        uuids::uuid roomId;
        {
            std::lock_guard<std::mutex> lock(_uuidMutex);
            roomId = _uuidGen();
        }
        auto newRoom = Room::Create(_ioManager, _sessionManager, roomId);
        newRoom->SetRemoveRoomCallback([weakSelf = weak_from_this()](const std::shared_ptr<Room>& removeRoom) {
            if(auto self = weakSelf.lock())
                self->RemoveRoom(removeRoom);
        });

        std::queue<std::pair<uuids::uuid, std::weak_ptr<Session>>> matchedSessions;
        for(auto cnt = 0; cnt < MATCHING_PLAYERS; ++cnt)
        {
            auto nextSession = _waitingQueue.front();
            _waitingQueue.pop_front();
            matchedSessions.push(nextSession);
            _weakSessions.erase(nextSession.first);
        }

        // Room Initialize
        std::lock_guard<std::mutex> roomsLock(_activeRoomsMutex);
        _activeRooms.push_back({ newRoom->GetId(), newRoom });
        _registerRoomToServerHandler(newRoom);

        // After Room Initialized Add Sessions
        while(!matchedSessions.empty())
        {
            auto [nextSessionId, weakNextSession] = matchedSessions.front();
            matchedSessions.pop();

            if(auto nextSession = weakNextSession.lock())
            {
                nextSession->SetRoom(newRoom->GetId());
                newRoom->AddSession(nextSessionId, weakNextSession);
                nextSession->RemoveDisconnectCallback(_sessionCallbackHandles[nextSessionId]);
                _sessionCallbackHandles.erase(nextSessionId);
            }
        }

        newRoom->WorldInit();
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

void Matching::RemoveSession(std::weak_ptr<Session> weakRemoveSession)
{
    std::lock_guard<std::mutex> waitingQueueLock(_waitingQueueMutex);
    if(auto removeSession = weakRemoveSession.lock())
    {
        auto removeId = removeSession->GetId();
        if(_weakSessions.find(removeId) == _weakSessions.end())
        {
            spdlog::error("matching: invalid access session {}", uuids::to_string(removeId));
            return;
        }

        auto it = std::find_if(_waitingQueue.begin(), _waitingQueue.end(), [removeId](const auto& p) { return p.first == removeId; });
        if(it == _waitingQueue.end())
            return;

        _waitingQueue.erase(it);                 // remove from queue
        _weakSessions.erase(removeId);           // remove from session map
        _sessionCallbackHandles.erase(removeId); // remove from session callback handle
        spdlog::info("matching: removed session {} from waiting queue", uuids::to_string(removeId));
    }
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