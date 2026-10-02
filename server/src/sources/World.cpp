#include "World.hpp"
#include "GameRules.hpp"
#include "ServerPolicy.hpp"
#include "Room.hpp"
#include "PacketPool.hpp"
#include "RuntimeMetrics.hpp"

void World::Init(const std::unordered_map<uuids::uuid, Participant> &sessions, std::weak_ptr<Room> room)
{
    _weakRoom = room;
    std::lock_guard playerLock(_playerMutex);
    _playerSize = sessions.size();

    int index = 0;
    for (const auto &[id, session] : sessions)
    {
        auto newPlayer = std::make_unique<Player>();
        newPlayer->position =
            Vector3((static_cast<float>(index) - GameRules::SpawnCenterIndex) * GameRules::SpawnSpacing, 0.0f,
                    0.0f); // 중앙 기준 스폰 위치 지정
        _players.insert({id, std::move(newPlayer)});
        _sessions.insert({id, session});
        index++;
    }

    DivideTeam();

    // 각 세션에 배정된 팀, 초기 스폰 위치, 서버 권위 물리/전투 스탯 및 전체 플레이어 초기 정보를 InfoHandshake(TCP)로
    // 전송
    for (const auto &[id, weakSession] : _sessions)
    {
        auto playerIt = _players.find(id);
        if (playerIt == _players.end() || !playerIt->second)
            continue;

        const auto &player = playerIt->second;

        Protocol::InfoHandshakePacket infoPacket;
        infoPacket.set_sessionid(uuids::to_string(id));
        infoPacket.set_presetid(weakSession.presetId);
        infoPacket.set_teamid(static_cast<std::int32_t>(player->teamType));
        infoPacket.set_spawnx(player->position.x);
        infoPacket.set_spawny(player->position.y);
        infoPacket.set_spawnz(player->position.z);
        infoPacket.set_movespeed(BASE_MOVE_SPEED);
        infoPacket.set_sprintspeed(static_cast<float>(MAX_SPEED));
        infoPacket.set_jumpspeed(JUMP_SPEED);
        infoPacket.set_gravity(GRAVITY);
        infoPacket.set_maxhp(player->hp);
        infoPacket.set_attackpower(player->attackPower);
        infoPacket.set_maxammo(player->ammo);

        for (const auto &[otherId, otherPlayer] : _players)
        {
            if (!otherPlayer)
                continue;

            auto *pInfo = infoPacket.add_players();
            pInfo->set_playerid(uuids::to_string(otherId));
            pInfo->set_teamid(static_cast<std::int32_t>(otherPlayer->teamType));
            pInfo->set_spawnx(otherPlayer->position.x);
            pInfo->set_spawny(otherPlayer->position.y);
            pInfo->set_spawnz(otherPlayer->position.z);
        }

        std::string serializedInfo;
        if (infoPacket.SerializeToString(&serializedInfo))
        {
            if (auto owner = _weakRoom.lock())
                owner->SendTo({id, weakSession.generation}, Protocol::PacketType::InfoHandshake,
                              std::move(serializedInfo), Transport::Tcp);
        }
    }

    spdlog::info("world(room id) {}: created and sent InfoHandshake to {} players", uuids::to_string(_roomId),
                 _playerSize);
}

void World::DivideTeam()
{
    // sample team divide (TODO: include role, rating ...)
    std::vector<Player *> playersTemp;
    playersTemp.reserve(_players.size());
    for (const auto &[id, player] : _players)
    {
        playersTemp.emplace_back(player.get());

        /*if(const auto team = static_cast<TeamType>(_dis(_gen)); team == TeamType::TeamA && _teamACount < 5)
            player->teamType = TeamType::TeamA;
        else
            player->teamType = TeamType::TeamB;*/
    }

    std::random_device rd;
    std::mt19937_64 gen(rd());
    std::ranges::shuffle(playersTemp, gen);

    int i = 0;
    for (i; i < playersTemp.size() / GameRules::TeamCount; ++i)
    {
        playersTemp[i]->teamType = TeamType::TeamA;
    }

    for (i; i < playersTemp.size(); ++i)
    {
        playersTemp[i]->teamType = TeamType::TeamB;
    }
}

void World::Move(uuids::uuid playerId, Vector3 direction, std::int32_t speed)
{
    std::lock_guard playerLock(_playerMutex);
    auto it = _players.find(playerId);
    if (it == _players.end() || !it->second)
    {
        spdlog::info("world(room id) {}: no player {}", uuids::to_string(_roomId), uuids::to_string(playerId));
        return;
    }

    it->second->Move(direction, speed);
}

void World::Jump(uuids::uuid player)
{
    std::lock_guard lock(_playerMutex);
    auto it = _players.find(player);
    if (it == _players.end() || !it->second)
    {
        spdlog::error("world(room id) {}: player {} is not found.", uuids::to_string(_roomId),
                      uuids::to_string(player));
        return;
    }

    it->second->Jump();
    it->second->needsStateBroadcast = true;
}

void World::Shoot(uuids::uuid shooterId, Vector3 direction, std::size_t targetTick)
{
    std::lock_guard lock(_playerMutex);
    _combatSystem.Shoot(shooterId, direction, targetTick, _players, _teamInfos, _weakRoom);
}

void World::Hit(uuids::uuid hitId, std::int32_t damage, uuids::uuid shooterId)
{
    std::lock_guard lock(_playerMutex);
    _combatSystem.OnHit(hitId, damage, shooterId, _players, _teamInfos, _weakRoom);
}

std::unique_ptr<GameResult> World::GetResult()
{
    std::lock_guard lock(_playerMutex);
    return std::make_unique<GameResult>(_teamInfos);
}

void World::StartUpdate(std::weak_ptr<Room> weakRoom, const std::chrono::microseconds interval)
{
    if (_isUpdating.exchange(true))
        return;
    _weakRoom = weakRoom;
    _tickInterval = interval;
    _lastTickTime = std::chrono::steady_clock::now();
    _timer.expires_at(_lastTickTime);
    ScheduleNextTick();
}

void World::StopUpdate()
{
    if (!_isUpdating.exchange(false))
        return;
    _timer.cancel();
}

void World::EnqueuePacket(std::shared_ptr<Protocol::IngamePacket> packet)
{
    std::lock_guard lock(_queueMutex);
    if (_packetQueue.size() < ServerPolicy::WorldInputs)
        _packetQueue.push(packet);
}

void World::ScheduleNextTick()
{
    if (!_isUpdating)
        return;

    _timer.expires_at(_timer.expiry() + _tickInterval);
    _timer.async_wait(asio::bind_executor(_strand, [weakRoom = _weakRoom, roomId = _roomId](const std::error_code &ec) {
        if (ec)
        {
            if (ec == asio::error::operation_aborted)
            {
                spdlog::info("world(room id) {}: update timer cancelled", uuids::to_string(roomId));
            }
            else
            {
                spdlog::error("world(room id) {}: update timer error: {}", uuids::to_string(roomId), ec.message());
            }
            return;
        }

        if (const auto room = weakRoom.lock())
        {
            if (const auto world = room->GetWorld())
            {
                auto now = std::chrono::steady_clock::now();
                constexpr int MAX_CATCHUP_TICKS = ServerPolicy::MaximumCatchupTicks;

                // 극단적인 랙 발생 시에만 강제 리셋
                if (now - world->_timer.expiry() > world->_tickInterval * MAX_CATCHUP_TICKS)
                {
                    world->_timer.expires_at(now);
                    spdlog::warn("world(room id) {}: heavy lag detected. resetting timer anchor.",
                                 uuids::to_string(roomId));
                }

                // StopUpdate 이후 완료된 handler는 world state를 변경하지 않는다.
                if (world->_isUpdating)
                {
                    world->Update();
                    world->ScheduleNextTick();
                }
            }
        }
    }));
}

void World::Update()
{
    const auto now = std::chrono::steady_clock::now();
    float actualDt = std::chrono::duration<float>(now - _lastTickTime).count();
    _lastTickTime = now;

    constexpr float defaultDt = GameRules::DefaultDeltaSeconds;
    if (actualDt <= GameRules::MovementEpsilon)
        actualDt = defaultDt;
    else if (actualDt > GameRules::MaximumTickDeltaSeconds)
        actualDt = GameRules::MaximumTickDeltaSeconds;

    // 1. Process queued inputs from clients
    ProcessQueue();

    // 2. Update world/physics state using actual elapsed delta time
    UpdateState(actualDt);

    // 3. Broadcast updated player states to all clients in the room
    if (const auto room = _weakRoom.lock())
    {
        std::lock_guard playerLock(_playerMutex);
        for (auto &[id, player] : _players)
        {
            if (!player)
                continue;

            const bool isMovingOrAirborne =
                (player->velocity.magnitude() > GameRules::MovementEpsilon) || !player->isGrounded;
            if (!isMovingOrAirborne && !player->needsStateBroadcast)
                continue;

            if (!isMovingOrAirborne)
            {
                player->needsStateBroadcast = false;
            }

            auto ingamePacket = IngamePacketPool::GetInstance()->Rent();
            ingamePacket->set_sessionid(uuids::to_string(id));
            ingamePacket->set_roomid(uuids::to_string(_roomId));
            ingamePacket->set_method(Protocol::IngameType::Move);
            ingamePacket->set_clienttick(_tickCount.load());

            // Serialize MovePacket to bytes data
            Protocol::MovePacket movePacket;
            movePacket.set_playerid(uuids::to_string(id));
            movePacket.set_originx(player->position.x);
            movePacket.set_originy(player->position.y);
            movePacket.set_originz(player->position.z);
            movePacket.set_dirx(player->velocity.x);
            movePacket.set_diry(player->velocity.y);
            movePacket.set_dirz(player->velocity.z);

            std::string serializedMove;
            if (movePacket.SerializeToString(&serializedMove))
            {
                ingamePacket->set_data(serializedMove);
            }

            std::string sendBuffer;
            if (ingamePacket->SerializeToString(&sendBuffer))
            {
                room->Broadcast(Protocol::PacketType::Ingame, std::move(sendBuffer), Transport::Udp,
                                StateKey{StateKind::Movement, id});
            }
        }
    }

    ++_tickCount;

    if (_tickCount % ServerPolicy::ScoreboardTickInterval == 0)
    {
        BroadcastScoreboard();
    }

    // 4. Check match end condition after all tick updates and broadcasts complete
    CheckMatchEnd();
    RuntimeMetrics::Observe("tick", std::chrono::steady_clock::now() - now);
}

void World::CheckMatchEnd()
{
    bool isMatchFinished = false;
    int winningTeam = -1;
    {
        std::lock_guard lock(_playerMutex);
        for (const auto &info : _teamInfos)
        {
            if (info.kills >= TARGET_KILLS)
            {
                isMatchFinished = true;
                winningTeam = static_cast<int>(info.teamType);
                break;
            }
        }
    }

    if (isMatchFinished)
    {
        spdlog::info("world {}: team {} reached target kills ({})! finishing match...", uuids::to_string(_roomId),
                     winningTeam, TARGET_KILLS);
        if (const auto room = _weakRoom.lock())
        {
            room->OnMatchFinished();
        }
    }
}

void World::ProcessQueue()
{
    std::queue<std::shared_ptr<Protocol::IngamePacket>> localQueue;
    {
        std::lock_guard lock(_queueMutex);
        std::swap(localQueue, _packetQueue);
    }

    while (!localQueue.empty())
    {
        auto packet = localQueue.front();
        localQueue.pop();

        auto playerIdOpt = uuids::uuid::from_string(packet->sessionid());
        if (!playerIdOpt.has_value())
        {
            spdlog::warn("world(room id) {}: invalid session id in packet", uuids::to_string(_roomId));
            continue;
        }

        uuids::uuid playerId = playerIdOpt.value();

        switch (packet->method())
        {
        case Protocol::IngameType::Move: {
            Protocol::MovePacket movePacket;
            if (movePacket.ParseFromString(packet->data()))
            {
                Vector3 origin(movePacket.originx(), movePacket.originy(), movePacket.originz());
                Vector3 direction(movePacket.dirx(), movePacket.diry(), movePacket.dirz());
                if (!std::isfinite(origin.x) || !std::isfinite(origin.y) || !std::isfinite(origin.z) ||
                    !std::isfinite(direction.x) || !std::isfinite(direction.y) || !std::isfinite(direction.z))
                    break;

                const auto packetNow = std::chrono::steady_clock::now();
                constexpr float defaultDt = GameRules::DefaultDeltaSeconds;

                bool isValid = true;

                {
                    std::lock_guard playerLock(_playerMutex);
                    if (_players.contains(playerId) && _players[playerId])
                    {
                        auto &player = _players[playerId];

                        float packetDt = defaultDt;
                        if (player->hasReceivedMovePacket)
                        {
                            packetDt = std::chrono::duration<float>(packetNow - player->lastMovePacketTime).count();
                            if (packetDt < defaultDt)
                                packetDt = defaultDt;
                            else if (packetDt > GameRules::MaximumInputDeltaSeconds)
                                packetDt = GameRules::MaximumInputDeltaSeconds;
                        }
                        player->lastMovePacketTime = packetNow;
                        player->hasReceivedMovePacket = true;

                        // 패킷 간 실제 경과 시간(packetDt) 기준 최대 이동 가능 거리 계산 (스프린트 속도 포함)
                        float maxSpeed = static_cast<float>(MAX_SPEED);
                        float theoreticalDist = maxSpeed * packetDt;

                        float tolerance =
                            GameRules::MovementDistanceTolerance; // 네트워크 지터 및 프레임 간격 완충 거리
                        float maxAllowedDist = theoreticalDist + tolerance;

                        // 서버 위치와 클라이언트 신규 위치 간 수평 거리 측정 (수직 Y축은 점프/중력 및
                        // CharacterController 높이 오프셋 분리)
                        float dx = player->position.x - origin.x;
                        float dz = player->position.z - origin.z;
                        float actualDist = std::sqrt(dx * dx + dz * dz);

                        // 차이가 허용된 수치를 넘음
                        if (actualDist > maxAllowedDist)
                        {
                            isValid = false;
                            player->velocity.x = 0.0f;
                            player->velocity.z = 0.0f;
                            spdlog::warn("world(room id) {}: player {} teleport suspected. Dist: {}m, Allowed: {}m "
                                         "(packetDt: {:.4f}s)",
                                         uuids::to_string(_roomId), uuids::to_string(playerId), actualDist,
                                         maxAllowedDist, packetDt);
                        }
                        else
                        {
                            player->position = origin;
                            player->updatedByPacketThisTick = true;
                            player->needsStateBroadcast = true;
                        }
                    }
                }

                if (isValid)
                {
                    float magnitude =
                        std::sqrt(direction.x * direction.x + direction.y * direction.y + direction.z * direction.z);
                    Vector3 normDirection(0.0f, 0.0f, 0.0f);
                    std::int32_t moveSpeed = 0;

                    if (magnitude > GameRules::MovementEpsilon)
                    {
                        normDirection.x = direction.x / magnitude;
                        normDirection.y = direction.y / magnitude;
                        normDirection.z = direction.z / magnitude;
                        moveSpeed = (magnitude > GameRules::SprintMagnitudeThreshold)
                                        ? MAX_SPEED
                                        : static_cast<std::int32_t>(BASE_MOVE_SPEED);
                    }

                    Move(playerId, normDirection, moveSpeed);
                }
            }
            break;
        }
        case Protocol::IngameType::Jump: {
            Jump(playerId);
            break;
        }
        case Protocol::IngameType::Shoot: {
            Vector3 direction(0.0f, 0.0f, 1.0f); // Test forward

            if (packet->data().size() >= sizeof(Vector3))
            {
                std::memcpy(&direction.x, packet->data().data(), sizeof(Vector3));
            }

            if (std::isfinite(direction.x) && std::isfinite(direction.y) && std::isfinite(direction.z))
                Shoot(playerId, direction, packet->clienttick());
            break;
        }
        case Protocol::IngameType::Hit: {
            spdlog::info("world(room id) {}: player {} hit", uuids::to_string(_roomId), uuids::to_string(playerId));
            break;
        }
        default:
            break;
        }
    }
}

void World::UpdateState(float dt)
{
    std::lock_guard playerLock(_playerMutex);

    for (auto &[id, player] : _players)
    {
        if (player)
        {
            player->RecordSnapshot(_tickCount.load());
            player->SimulatePhysics(dt, GRAVITY);
        }
    }
}

bool World::GetPlayerPosition(uuids::uuid playerId, Vector3 &outPosition)
{
    std::lock_guard playerLock(_playerMutex);
    const auto it = _players.find(playerId);
    if (it == _players.end() || !it->second)
    {
        return false;
    }
    outPosition = it->second->position;
    return true;
}

void World::BroadcastScoreboard(bool coalesce)
{
    Protocol::ScoreboardPacket scorePacket;
    scorePacket.set_roomid(uuids::to_string(_roomId));
    {
        std::lock_guard lock(_playerMutex);
        for (const auto &[id, player] : _players)
        {
            if (player)
            {
                auto *score = scorePacket.add_scores();
                score->set_playerid(uuids::to_string(id));
                score->set_kill(player->kill);
                score->set_death(player->death);
                score->set_heal(player->heal);
                score->set_teamid(static_cast<std::int32_t>(player->teamType));
                score->set_damage(player->damage);
            }
        }

        if (_teamInfos.size() >= 2)
        {
            scorePacket.set_teamascore(_teamInfos[0].kills);
            scorePacket.set_teambscore(_teamInfos[1].kills);
            if (_teamInfos[0].kills > _teamInfos[1].kills)
                scorePacket.set_winningteam(static_cast<std::int32_t>(TeamType::TeamA));
            else if (_teamInfos[1].kills > _teamInfos[0].kills)
                scorePacket.set_winningteam(static_cast<std::int32_t>(TeamType::TeamB));
            else
                scorePacket.set_winningteam(static_cast<std::int32_t>(TeamType::Draw));
        }
    }

    std::string serializedScoreboard;
    if (!scorePacket.SerializeToString(&serializedScoreboard))
    {
        spdlog::error("world(room id) {}: failed to serialize scoreboard", uuids::to_string(_roomId));
        return;
    }

    Protocol::IngamePacket ingamePacket;
    ingamePacket.set_roomid(uuids::to_string(_roomId));
    ingamePacket.set_method(Protocol::IngameType::Score);
    ingamePacket.set_data(serializedScoreboard);
    ingamePacket.set_clienttick(_tickCount.load());

    std::string serializedIngamePacket;
    if (!ingamePacket.SerializeToString(&serializedIngamePacket))
    {
        spdlog::error("world(room id) {}: failed to serialize ingamePacket", uuids::to_string(_roomId));
        return;
    }

    // 밀림 발생 시 가장 최신 데이터를 전송하도록 보장
    if (auto room = _weakRoom.lock())
        room->Broadcast(Protocol::PacketType::Ingame, std::move(serializedIngamePacket), Transport::Tcp,
                        coalesce ? std::optional<StateKey>{StateKey{StateKind::Scoreboard, _roomId}} : std::nullopt);
}