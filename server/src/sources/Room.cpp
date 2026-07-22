#include "Room.hpp"

#include "HttpResultReporter.hpp"
#include "IOManager.hpp"
#include "Session.hpp"

Room::Room(SecretKey, std::shared_ptr<IOManager> ioManager, uuids::uuid roomId, std::size_t expectedPlayerCount)
    : _ioManager(ioManager), _roomId(roomId), _expectedPlayerCount(expectedPlayerCount), _world(std::make_unique<World>(ioManager->GetIoContext(), roomId))
{
}

Room::~Room()
{
    spdlog::info("room: destroyed", uuids::to_string(_roomId));
}

void Room::OnMatchFinished()
{
    spdlog::info("room: match finished. reporting results to auth server...");

    auto winningTeam = 1; // TODO : 실제 이긴 팀을 계산하여 반환하는 World::GetWinner 함수 구현 필요
    auto playerStats = _world->GetPlayerStats();

    json j;
    j["apiKey"] = "default_secret_key"; // TODO : 검증용 api key 전달 로직 구현 필요
    j["matchId"] = uuids::to_string(_roomId);
    j["winningTeam"] = winningTeam;
    j["TeamAScore"] = 0; // TODO : 팀 별 스코어 계산 로직 필요
    j["TeamBScore"] = 0; // TODO : 팀 별 스코어 계산 로직 필요

    // ISO8601 UTC Time
    j["endTimeUtc"] = std::format("{:%FT%TZ}", std::chrono::system_clock::now());

    j["winnerUserIds"] = json::array();
    j["playerStats"] = json::array();
    for(const auto& stat : *playerStats)
    {
        if(static_cast<int>(stat.team) == winningTeam)
            j["winnerUserIds"].push_back(uuids::to_string(stat.id));

        j["playerStats"].push_back({
            { "userId", uuids::to_string(stat.id) },
            { "kills", stat.kill },
            { "deaths", stat.death },
            { "assists", stat.assist },
            { "damage", stat.damage },
            { "heals", stat.damage },
            { "guards", stat.guard }
        });
    }
    
    std::string jsonPayload = j.dump();

    if(HttpResultReporter::SendMatchResult("127.0.0.1", 9000, jsonPayload))
        spdlog::info("room: match result successfully reported to auth server.");
    else
        spdlog::error("room: failed to report match result");
    
    // 프로세스 종료
    std::exit(0);
}

void Room::TryStartGameNoLock()
{
    if(_isWorldStarted)
        return;

    if(_sessions.size() >= _expectedPlayerCount)
    {
        if(!_isWorldStarted.exchange(true))
        {
            spdlog::info("room: all players connected! starting world...", uuids::to_string(_roomId));
            WorldInitNoLock();
        }
    }
}

void Room::WorldInitNoLock()
{
    _world->Init(_sessions);
    spdlog::info("room: world create complete", uuids::to_string(_roomId));

    _world->StartUpdate(weak_from_this());
}

void Room::Stop()
{
    _world->StopUpdate();

    if(!_sessions.empty())
    {
        _sessions.clear();
    }
}

void Room::AddSession(uuids::uuid sessionId, std::weak_ptr<Session> weakSession)
{
    std::lock_guard<std::mutex> lock(_sessionsMutex);
    if(const auto session = weakSession.lock())
    {
        _sessions.insert({ sessionId, weakSession });
        session->AddDisconnectCallback([weakSelf = weak_from_this()](const std::weak_ptr<Session>& removeSession) {
            if(const auto self = weakSelf.lock())
                self->RemoveSession(removeSession);
        });
    }

    TryStartGameNoLock();
}

void Room::RemoveSession(std::weak_ptr<Session> weakRemoveSession)
{
    std::lock_guard<std::mutex> lock(_sessionsMutex);
    if(const auto removeSession = weakRemoveSession.lock())
    {
        if(_sessions.erase(removeSession->GetId()) == 0)
        {
            return;
        }

        spdlog::info("room: remove session {}", uuids::to_string(_roomId), uuids::to_string(removeSession->GetId()));

        if(!_sessions.empty())
            return;

        spdlog::info("room: all session removed", uuids::to_string(_roomId));
        _world->StopUpdate();
    }
}

void Room::Broadcast(std::shared_ptr<Packet> packet)
{
    for(const auto& [id, weakSession] : _sessions)
    {
        if(const auto session = weakSession.lock())
        {
            if(!session->IsValid())
                continue;
            session->EnqueueUdpSendPacket(packet);
        }
    }
}

void Room::EnqueuePacket(std::shared_ptr<IngamePacket> packet) const
{
    _world->EnqueuePacket(packet);
}