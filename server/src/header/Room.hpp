#pragma once

#include <memory>
#include <mutex>
#include <unordered_map>
#include <uuid.h>
#include <nlohmann/json.hpp>

#include "Packet.pb.h"
#include "World.hpp"
using namespace Protocol;
using json = nlohmann::json;

class IOManager;
class SessionManager;
class Session;

class Room : public std::enable_shared_from_this<Room>
{
private:
    struct SecretKey
    {
    };

public:
    explicit Room(SecretKey, std::shared_ptr<IOManager> ioManager, uuids::uuid matchId, const std::string apiKey, std::size_t expectedPlayerCount);
    ~Room();

    static auto Create(std::shared_ptr<IOManager> ioManager, uuids::uuid matchId, const std::string apiKey, std::size_t expectedPlayerCount)
    {
        auto newRoom = std::make_shared<Room>(SecretKey{}, ioManager, matchId, apiKey, expectedPlayerCount);
        return newRoom;
    }
    
private:
    void OnMatchFinished();

public:
    void TryStartGameNoLock();
    void WorldInitNoLock();
    void Stop();
    void AddSession(uuids::uuid sessionId, std::weak_ptr<Session> session);
    void RemoveSession(std::weak_ptr<Session> removeSession);
    void Broadcast(std::shared_ptr<Packet> packet);
    void EnqueuePacket(std::shared_ptr<IngamePacket> packet) const;

    uuids::uuid GetId() const
    {
        return _matchId;
    }

    World* GetWorld() const { return _world.get(); }

private:
    std::shared_ptr<IOManager> _ioManager;
    std::shared_ptr<SessionManager> _sessionManager;
    uuids::uuid _matchId;
    std::string _apiKey;
    const std::string _serverHost = "127.0.0.1"; // 인증 서버 호스트
    const std::uint16_t _serverPort = 9000; // 인증 서버 포트

    std::size_t _expectedPlayerCount{ 0 }; // 방에 들어와야 할 총 유저 수
    std::atomic<bool> _isWorldStarted{ false }; // 중복 실행 방지 플래그

    std::unordered_map<uuids::uuid, std::weak_ptr<Session>> _sessions;
    std::mutex _sessionsMutex;

    // World information
    std::unique_ptr<World> _world;
};