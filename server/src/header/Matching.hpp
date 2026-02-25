#pragma once

#include <condition_variable>
#include <memory>
#include <mutex>
#include <queue>
#include <stack>

#include <uuid.h>

#include "IOManager.hpp"

class Session;

constexpr std::uint8_t MATCHING_PLAYERS = 10;

class Matching : public std::enable_shared_from_this<Matching>
{
private:
    struct SecretKey {};

public:
    explicit Matching(SecretKey, std::shared_ptr<IOManager> ioManager) : _ioManager(ioManager), _isRunning(false) {}
    ~Matching()
    {
        spdlog::info("matching destroyed");
    }
    static std::shared_ptr<Matching> Create(std::shared_ptr<IOManager> ioManager)
    {
        auto newMatching = std::make_shared<Matching>(SecretKey{}, ioManager);
        return newMatching;
    }

public:
    void AddWaitSession(uuids::uuid waitSessionInfo, std::shared_ptr<Session> session);
    void Start();
    void Stop();
    void MatchMaking();

private:
    void RemoveSession(std::shared_ptr<Session> removeSession);


private:
    std::shared_ptr<IOManager> _ioManager;

    std::deque<std::pair<uuids::uuid,std::shared_ptr<Session>>> _waitingQueue;
    std::mutex _waitingQueueMutex;
    std::condition_variable _waitingCv;

    std::atomic<bool> _isRunning;
};