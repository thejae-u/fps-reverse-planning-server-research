#include "Matching.hpp"

void Matching::AddWaitSession(uuids::uuid waitSessionInfo)
{
    std::lock_guard<std::mutex> queueLock(_waitingQueueMutex);
    _waitingQueue.push(waitSessionInfo);
}

void Matching::MatchMaking()
{
    auto selfWeak(weak_from_this());
    std::unique_lock<std::mutex> queueLock(_waitingQueueMutex);

    // waiting for matching player
    _waitingCv.wait(queueLock, [selfWeak]() {
        if(auto selfShared = selfWeak.lock())
            return selfShared->_waitingQueue.size() >= MATCHING_PLAYERS;
    });

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
