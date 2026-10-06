#include "Room.hpp"
#include "GameRules.hpp"
#include "ServerPolicy.hpp"
#include "HttpResultReporter.hpp"
#include <future>

Room::Room(SecretKey, std::shared_ptr<ExecutionContext> game, std::shared_ptr<BlockingExecutor> blocking,
           uuids::uuid matchId, const std::string &token, std::size_t expected)
    : _game(std::move(game)), _blocking(std::move(blocking)), _strand(_game->GetIoContext()), _matchId(matchId),
      _authToken(token), _expectedPlayerCount(expected),
      _world(std::make_unique<World>(_game->GetIoContext(), _strand, matchId))
{
}

void Room::AddSession(uuids::uuid id, Participant participant)
{
    std::lock_guard lock(_submissionMutex);
    if (!_acceptingPosts)
        return;
    asio::post(_strand, [self = shared_from_this(), id, participant] {
        if (self->_stopped || self->_isMatchFinished || self->_isWorldStarted)
            return;
        self->_sessions.insert_or_assign(id, participant);
        if (self->_sessions.size() == self->_expectedPlayerCount)
        {
            self->_isWorldStarted = true;
            self->_world->Init(self->_sessions, self);
            self->_world->StartUpdate(self);
        }
    });
}

void Room::RemoveSession(uuids::uuid id, std::uint64_t generation)
{
    std::lock_guard lock(_submissionMutex);
    if (!_acceptingPosts)
        return;
    asio::post(_strand, [self = shared_from_this(), id, generation] {
        if (self->_stopped)
            return;
        auto it = self->_sessions.find(id);
        if (it == self->_sessions.end() || it->second.generation != generation)
            return;
        self->_sessions.erase(it);
        if (self->_sessions.empty())
            self->OnMatchFinished();
    });
}

void Room::EnqueuePacket(std::shared_ptr<IngamePacket> packet, Recipient sender)
{
    std::lock_guard lock(_submissionMutex);
    if (!_acceptingPosts)
        return;
    asio::post(_strand, [self = shared_from_this(), packet = std::move(packet), sender] {
        if (self->_stopped || self->_isMatchFinished || !self->_isWorldStarted)
            return;
        auto it = self->_sessions.find(sender.id);
        if (it == self->_sessions.end() || it->second.generation != sender.generation)
            return;
        self->_world->EnqueuePacket(packet);
    });
}

void Room::Broadcast(PacketType type, std::string payload, Transport transport, std::optional<StateKey> stateKey) const
{
    if (auto gateway = _gateway.lock())
    {
        OutboundMessage message{
            type, transport, {}, std::make_shared<const std::string>(std::move(payload)), std::move(stateKey)};
        for (const auto &[id, participant] : _sessions)
            message.recipients.push_back({id, participant.generation});
        gateway->PostSend(std::move(message));
    }
}

void Room::SendTo(Recipient recipient, PacketType type, std::string payload, Transport transport) const
{
    if (auto gateway = _gateway.lock())
        gateway->PostSend({type, transport, {recipient}, std::make_shared<const std::string>(std::move(payload)), {}});
}

void Room::Stop()
{
    auto done = std::make_shared<std::promise<void>>();
    auto future = done->get_future();
    auto stop = [self = shared_from_this(), done] {
        self->_stopped = true;
        self->_world->StopUpdate();
        self->_sessions.clear();
        done->set_value();
    };
    {
        std::lock_guard lock(_submissionMutex);
        _acceptingPosts = false;
        if (_strand.running_in_this_thread())
            stop();
        else
            asio::post(_strand, std::move(stop));
    }
    future.get();
}

void Room::OnMatchFinished()
{
    if (_isMatchFinished || _stopped)
        return;
    _isMatchFinished = true;
    _world->StopUpdate();
    // 최종 점수판은 병합/용량 초과 시 교체 가능한 상태 제거 대상에서 제외한다.
    _world->BroadcastScoreboard(false);

    Broadcast(PacketType::EndGame, {}, Transport::Tcp);
    Broadcast(PacketType::EndGame, {}, Transport::Udp);

    const auto playerStats = _world->GetPlayerStats();
    const auto gameResult = _world->GetResult();
    auto winningTeam = static_cast<int>(gameResult->winningTeam);

    auto toCompactUuid = [](const uuids::uuid &id) {
        std::string s = uuids::to_string(id);
        std::erase(s, '-');
        return s;
    };

    json j;
    j["matchId"] = toCompactUuid(_matchId);
    j["winningTeam"] = winningTeam;
    j["TeamAScore"] = gameResult->teamAInfo.kills;
    j["TeamBScore"] = gameResult->teamBInfo.kills;

    // ISO8601 UTC Time
    j["endTimeUtc"] = std::format("{:%FT%TZ}", std::chrono::system_clock::now());

    j["winnerUserIds"] = json::array();
    j["playerStats"] = json::array();

    // 팀 별 플레이어 스탯 저장
    for (const auto &stat : *playerStats)
    {
        if (static_cast<int>(stat.teamType) == winningTeam)
            j["winnerUserIds"].push_back(toCompactUuid(stat.id));

        j["playerStats"].push_back({{"team", static_cast<int>(stat.teamType)},
                                    {"userId", toCompactUuid(stat.id)},
                                    {"kills", stat.kill},
                                    {"deaths", stat.death},
                                    {"assists", stat.assist},
                                    {"damage", stat.damage},
                                    {"heals", stat.heal},
                                    {"guards", stat.guard}});
    }

    std::string jsonPayload = j.dump();

    _blocking->Post([self = shared_from_this(), jsonPayload = std::move(jsonPayload)] {
        if (!HttpResultReporter::SendMatchResult(self->_serverHost, self->_serverPort, jsonPayload, self->_authToken))
            spdlog::error("match result reporting failed");
        if (self->_shutdownCallback)
            self->_shutdownCallback();
    });
}

void Room::PostInput(std::string payload, Recipient sender)
{
    const auto submitted = BenchmarkSupport::Enabled() ? BenchmarkSupport::Clock::now()
                                                       : BenchmarkSupport::Clock::time_point{};
    std::lock_guard lock(_submissionMutex);
    if (!_acceptingPosts)
        return;
    if (_pendingInputs.fetch_add(1) >= ServerPolicy::PendingGameInputs)
    {
        _pendingInputs.fetch_sub(1);
        return;
    }
    asio::post(_strand, [self = shared_from_this(), payload = std::move(payload), sender, submitted] {
        self->_pendingInputs.fetch_sub(1);
        if (self->_stopped || self->_isMatchFinished || !self->_isWorldStarted)
            return;
        auto it = self->_sessions.find(sender.id);
        if (it == self->_sessions.end() || it->second.generation != sender.generation)
            return;
        auto packet = std::make_shared<IngamePacket>();
        if (!packet->ParseFromString(payload))
            return;
        const auto playerId = uuids::uuid::from_string(packet->sessionid());
        const auto roomId = uuids::uuid::from_string(packet->roomid());
        if (!playerId || !roomId || *playerId != sender.id || *roomId != self->_matchId)
            return;
        self->_world->EnqueuePacket(std::move(packet), submitted);
    });
}

void Room::PostHandshake(Recipient recipient, std::int32_t preset)
{
    std::lock_guard lock(_submissionMutex);
    if (!_acceptingPosts)
        return;
    asio::post(_strand, [self = shared_from_this(), recipient, preset] {
        if (self->_stopped)
            return;
        InfoHandshakePacket response;
        response.set_sessionid(uuids::to_string(recipient.id));
        response.set_presetid(preset);
        response.set_movespeed(BASE_MOVE_SPEED);
        response.set_sprintspeed(MAX_SPEED);
        response.set_jumpspeed(JUMP_SPEED);
        response.set_gravity(GRAVITY);
        response.set_maxhp(GameRules::MaximumHealth);
        response.set_attackpower(GameRules::HandshakeAttackPower);
        response.set_maxammo(GameRules::MaximumAmmo);
        std::string payload;
        if (response.SerializeToString(&payload))
            self->SendTo(recipient, PacketType::InfoHandshake, std::move(payload), Transport::Tcp);
    });
}
