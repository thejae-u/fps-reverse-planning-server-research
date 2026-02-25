#include "Matching.hpp"
#include "Session.hpp"

void Matching::AddWaitSession(uuids::uuid waitSessionInfo, std::shared_ptr<Session> session)
{
    std::lock_guard<std::mutex> queueLock(_waitingQueueMutex);
    _waitingQueue.push_back({ waitSessionInfo, session });

    auto weakSelf = weak_from_this();
    session->SetNotifyDisconnectCallback([weakSelf](const std::shared_ptr<Session>& removeSession) {
        if(auto sharedSelf = weakSelf.lock())
            sharedSelf->RemoveSession(removeSession);
    });

    _waitingCv.notify_one();
}

void Matching::Start()
{
    auto selfWeak(weak_from_this());
    _isRunning = true;

    _ioManager->RegisterWork([selfWeak]() {
        if(auto selfShared = selfWeak.lock())
            selfShared->MatchMaking();
    });
}

void Matching::Stop()
{
    std::lock_guard<std::mutex> queueLock(_waitingQueueMutex);
    _isRunning = false;

    _waitingCv.notify_all();

    while(!_waitingQueue.empty())
    {
        auto [id, session] = _waitingQueue.front();
        session->Stop();

        _waitingQueue.pop_front();
    }

    spdlog::info("matching stopped");
}

void Matching::MatchMaking()
{
    auto selfWeak(weak_from_this());
    std::unique_lock<std::mutex> queueLock(_waitingQueueMutex);

    spdlog::info("match making waiting...");

    // waiting for matching player
    _waitingCv.wait(queueLock, [selfWeak]() -> bool {
        if(auto selfShared = selfWeak.lock())
            return selfShared->_waitingQueue.size() >= MATCHING_PLAYERS || !selfShared->_isRunning;
        return true;
    });

    if(!_isRunning)
    {
        spdlog::info("server is off cancel matchmaking");
        return;
    }

    // Matching Sequence

    // Matching again
    if(auto selfShared = selfWeak.lock())
    {
        // register MatchMaking function (lambda)
        selfShared->_ioManager->RegisterWork([selfWeak]() {
            if(auto selfShared = selfWeak.lock())
            {
                selfShared->MatchMaking();
            }
        });
    }
}

void Matching::RemoveSession(std::shared_ptr<Session> removeSession)
{
    std::lock_guard<std::mutex> waitingQueueLock(_waitingQueueMutex);

    auto it = _waitingQueue.begin();
    auto removeId = removeSession->GetId();
    for(it; it->first != removeId; ++it)
    {
    }

    if (it == _waitingQueue.end())
    {
        spdlog::error("invalid session : {}", uuids::to_string(removeId));
        return;
    }

    _waitingQueue.erase(it);
    spdlog::info("removed {} from waiting queue", uuids::to_string(removeId));
}
