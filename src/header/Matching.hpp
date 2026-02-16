#pragma once

#include <memory>
#include <queue>
#include <mutex>
#include <condition_variable>

#include <uuid.h>

#include "IOManager.hpp"

constexpr std::uint8_t MATCHING_PLAYERS = 10;

class Matching : public std::enable_shared_from_this<Matching>
{
private:
    struct SecretKey {};

public:
    explicit Matching(SecretKey, std::shared_ptr<IOManager> ioManager) : _ioManager(ioManager) {}
    ~Matching() {}
    static std::shared_ptr<Matching> Create(std::shared_ptr<IOManager> ioManager)
    {
        auto newMatching = std::make_shared<Matching>(SecretKey{}, ioManager);
        return newMatching;
    }

public:
    void AddWaitSession(uuids::uuid waitSessionInfo);
    void MatchMaking();

private:
    std::shared_ptr<IOManager> _ioManager;

    std::queue<uuids::uuid> _waitingQueue;
    std::mutex _waitingQueueMutex;
    std::condition_variable _waitingCv;
};