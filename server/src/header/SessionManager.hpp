#pragma once
#include <memory>
#include <mutex>
#include <unordered_map>
#include <uuid.h>
#include <spdlog/spdlog.h>

class Session;

class SessionManager : public std::enable_shared_from_this<SessionManager>
{
private:
    struct SecretKey {};

public:
    explicit SessionManager(SecretKey) { spdlog::info("session manager: initialized complete"); }
    ~SessionManager() { spdlog::info("sesion manager destroyed"); }

    static std::shared_ptr<SessionManager> Create()
    {
        auto newSessionManager = std::make_shared<SessionManager>(SecretKey{});
        return newSessionManager;
    }

public:
    // Return Session or default from Session Manager
    std::weak_ptr<Session> Find(uuids::uuid id);
    // Insert Session to Session Manager
    std::weak_ptr<Session> Insert(uuids::uuid id, const std::shared_ptr<Session>& session);
    // Erase Session from Session Manager
    void Erase(uuids::uuid id);
    // Size of Sessions
    std::size_t Size();
    // Remove All Session from session Manager
    void Clear();

private:
    std::unordered_map<uuids::uuid, std::shared_ptr<Session>> _sessions;
    std::mutex _sessionsMutex;
};
