#include "Matching.hpp"

void Matching::AddWaitSession(uuids::uuid waitSessionInfo)
{
    std::lock_guard<std::mutex> queueLock(_waitingQueueMutex);
    _waitingQueue.push(waitSessionInfo);
}

void Matching::Start()
{
    auto selfWeak(weak_from_this());

    _ioManager->RegisterWork([selfWeak]() {
        if(auto selfShared = selfWeak.lock())
            selfShared->MatchMaking();
    });

    _isRunning = true;
}

void Matching::Stop()
{
    spdlog::info("Stop sign complete");
    {
        std::lock_guard<std::mutex> queueLock(_waitingQueueMutex);
        _isRunning = false;
    }

    _waitingCv.notify_all();
    spdlog::info("notifying complete");
}

void Matching::MatchMaking()
{
    auto selfWeak(weak_from_this());
    std::unique_lock<std::mutex> queueLock(_waitingQueueMutex);

    spdlog::info("Match Making Waiting...");

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