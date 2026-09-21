#include "World.hpp"
#include "Session.hpp"
#include "Room.hpp"
#include "PacketPool.hpp"

void World::Init(const std::unordered_map<uuids::uuid, std::weak_ptr<Session>>& sessions)
{
    std::lock_guard playerLock(_playerMutex);
    _playerSize = sessions.size();

    int index = 0;
    for(const auto& [id, session] : sessions)
    {
        auto newPlayer = std::make_unique<Player>();
        newPlayer->position = Vector3(static_cast<float>(index) * 5.0f, 0.0f, 0.0f); // 임시 위치 지정
        _players.insert({ id, std::move(newPlayer) });
        _sessions.insert({ id, session });
        index++;

        DivideTeam();
    }

    spdlog::info("world(room id) {}: created", uuids::to_string(_roomId));
}

void World::DivideTeam()
{
    // sample team divide (TODO: include role, rating ...)
    for(const auto& [id, player] : _players)
    {
        if(const auto team = static_cast<TeamType>(_dis(_gen)); team == TeamType::TeamA && _teamACount < 5)
            player->teamType = TeamType::TeamA;
        else
            player->teamType = TeamType::TeamB;
    }
}

void World::Move(uuids::uuid playerId, Vector3 direction, std::int32_t speed)
{
    std::lock_guard playerLock(_playerMutex);
    auto it = _players.find(playerId);
    if(it == _players.end() || !it->second)
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
    if(it == _players.end() || !it->second)
    {
        spdlog::error("world(room id) {}: player {} is not found.", uuids::to_string(_roomId), uuids::to_string(player));
        return;
    }

    it->second->Jump();
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
    return std::make_unique<GameResult>(_teamInfos);
}

void World::InitMockPlayers(const std::vector<std::string>& allowedPlayers)
{
    std::lock_guard playerLock(_playerMutex);
    _playerSize = allowedPlayers.size();

    int index = 0;
    for(const auto& token : allowedPlayers)
    {
        auto idOpt = uuids::uuid::from_string(token);
        auto id = idOpt.value_or(uuids::uuid_system_generator{}());

        auto newPlayer = std::make_unique<Player>();
        newPlayer->position = Vector3(static_cast<float>(index) * 5.0f, 0.0f, 0.0f);
        _players.insert({ id, std::move(newPlayer) });
        index++;
    }

    DivideTeam();
    spdlog::info("world(room id) {}: initialized {} mock players for test mode", uuids::to_string(_roomId), _players.size());
}

void World::SimulateKill(TeamType scoringTeam)
{
    std::lock_guard lock(_playerMutex);
    TeamType victimTeam = (scoringTeam == TeamType::TeamA) ? TeamType::TeamB : TeamType::TeamA;

    // 1. Scoring player 찾기
    for(auto& [id, player] : _players)
    {
        if(player && player->teamType == scoringTeam)
        {
            player->kill++;
            player->damage += 100;
            break;
        }
    }

    // 2. Victim player 찾기
    for(auto& [id, player] : _players)
    {
        if(player && player->teamType == victimTeam)
        {
            player->death++;
            break;
        }
    }

    // 3. TeamInfo 갱신
    std::int16_t currentKills = 0;
    for(auto& info : _teamInfos)
    {
        if(info.teamType == scoringTeam)
        {
            info.kills++;
            info.damages += 100;
            currentKills = info.kills;
        }
        else if(info.teamType == victimTeam)
        {
            info.deaths++;
        }
    }

    spdlog::info("[Simulation] Team {} scored! Total kills: {}",
                 (scoringTeam == TeamType::TeamA ? "A" : "B"), currentKills);

    // TARGET_KILLS 도달 시 자동 종료
    constexpr std::int16_t TARGET_KILLS = 5;
    if(currentKills >= TARGET_KILLS)
    {
        spdlog::info("[Simulation] Team {} reached target kills ({})! Finishing match...",
                     (scoringTeam == TeamType::TeamA ? "A" : "B"), TARGET_KILLS);
        if(const auto room = _weakRoom.lock())
        {
            room->OnMatchFinished();
        }
    }
}

void World::SetTestScores(int aKills, int bKills)
{
    std::lock_guard lock(_playerMutex);
    for(auto& info : _teamInfos)
    {
        if(info.teamType == TeamType::TeamA)
            info.kills = static_cast<std::int16_t>(aKills);
        else if(info.teamType == TeamType::TeamB)
            info.kills = static_cast<std::int16_t>(bKills);
    }
    spdlog::info("[Test] Test scores directly set: TeamA={}, TeamB={}", aKills, bKills);
}

void World::StartUpdate(std::weak_ptr<Room> weakRoom, const std::chrono::microseconds interval)
{
    if(_isUpdating.exchange(true))
        return; // Already updating

    _weakRoom = weakRoom;
    _tickInterval = interval;
    _timer.expires_at(std::chrono::steady_clock::now());
    ScheduleNextTick();
}

void World::StopUpdate()
{
    if(!_isUpdating.exchange(false))
        return; // Not updating

    _timer.cancel();
    spdlog::info("world(room id) {}: update loop stopped", uuids::to_string(_roomId));
}

void World::EnqueuePacket(std::shared_ptr<Protocol::IngamePacket> packet)
{
    std::lock_guard lock(_queueMutex);
    _packetQueue.push(packet);
}

void World::ScheduleNextTick()
{
    if(!_isUpdating)
        return;

    _timer.expires_at(_timer.expiry() + _tickInterval);
    _timer.async_wait([weakRoom = _weakRoom, roomId = _roomId](const std::error_code& ec) {
        if(ec)
        {
            if(ec == asio::error::operation_aborted)
            {
                spdlog::info("world(room id) {}: update timer cancelled", uuids::to_string(roomId));
            }
            else
            {
                spdlog::error("world(room id) {}: update timer error: {}", uuids::to_string(roomId), ec.message());
            }
            return;
        }

        if(const auto room = weakRoom.lock())
        {
            if(const auto world = room->GetWorld())
            {
                auto now = std::chrono::steady_clock::now();
                const int MAX_CATCHUP_TICKS = 5;

                // 극단적인 랙 발생 시에만 강제 리셋
                if(now - world->_timer.expiry() > world->_tickInterval * MAX_CATCHUP_TICKS)
                {
                    world->_timer.expires_at(now);
                    spdlog::warn("world(room id) {}: heavy lag detected. resetting timer anchor.", uuids::to_string(roomId));
                }

                // callback 1회 당 Update 1회 수행
                world->Update();
                world->ScheduleNextTick();
            }
        }
    });
}

void World::Update()
{
    // 1. Process queued inputs from clients
    ProcessQueue();

    // 2. Update world/physics state
    UpdateState();

    // 3. Broadcast updated player states to all clients in the room
    if(const auto room = _weakRoom.lock())
    {
        std::lock_guard playerLock(_playerMutex);
        for(const auto& [id, player] : _players)
        {
            if(!player)
                continue;

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
            if(movePacket.SerializeToString(&serializedMove))
            {
                ingamePacket->set_data(serializedMove);
            }

            std::string sendBuffer;
            if(ingamePacket->SerializeToString(&sendBuffer))
            {
                auto sendPacket = NetworkPacketPool::GetInstance()->Rent();
                sendPacket->set_type(Protocol::PacketType::Ingame);
                sendPacket->set_data(sendBuffer);
                room->Broadcast(std::move(sendPacket));
            }
        }
    }

    ++_tickCount;

    /*if(_tickCount % 60 == 0)
    {
        PrintScoreboard();
    }*/
}

void World::ProcessQueue()
{
    std::queue<std::shared_ptr<Protocol::IngamePacket>> localQueue;
    {
        std::lock_guard lock(_queueMutex);
        std::swap(localQueue, _packetQueue);
    }

    while(!localQueue.empty())
    {
        auto packet = localQueue.front();
        localQueue.pop();

        auto playerIdOpt = uuids::uuid::from_string(packet->sessionid());
        if(!playerIdOpt.has_value())
        {
            spdlog::warn("world(room id) {}: invalid session id in packet", uuids::to_string(_roomId));
            continue;
        }

        uuids::uuid playerId = playerIdOpt.value();

        switch(packet->method())
        {
        case Protocol::IngameType::Move: {
            Protocol::MovePacket movePacket;
            if(movePacket.ParseFromString(packet->data()))
            {
                Vector3 origin(movePacket.originx(), movePacket.originy(), movePacket.originz());
                Vector3 direction(movePacket.dirx(), movePacket.diry(), movePacket.dirz());

                // 서버 틱 기준 dt (16.6ms at 60fps)
                constexpr float div = 1'000'000.0f;
                const float dt = static_cast<float>(_tickInterval.count()) / div;
                
                bool isValid = true;
                
                {
                    std::lock_guard playerLock(_playerMutex);
                    if(_players.contains(playerId) && _players[playerId])
                    {
                        auto& player = _players[playerId];
                        
                        // 이론 최대 이동 거리 계산
                        float maxSpeed = BASE_MOVE_SPEED;
                        float theoreticalDist = maxSpeed * dt;
                        
                        float tolerance = 2.0f; // 레이턴시 극복용 완충 거리
                        float maxAllowedDist = theoreticalDist + tolerance;
                        
                        // 서버 위치와 클라이언트 신규 위치 간 실제 거리 측정
                        float dx = player->position.x - origin.x;
                        float dy = player->position.y - origin.y;
                        float dz = player->position.z - origin.z;
                        float actualDist = std::sqrt(dx * dx + dy * dy + dz * dz);
                        
                        // 차이가 허용된 수치를 넘음
                        if(actualDist > maxAllowedDist)
                        {
                            isValid = false;
                            spdlog::warn("world(room id) {}: player {} teleport suspected. Dist: {}m, Allowed: {}m",
                                uuids::to_string(_roomId), uuids::to_string(playerId), actualDist, maxAllowedDist);
                        }
                        else
                        {
                            player->position = origin;
                        }
                    }
                }
                
                if(isValid)
                {
                    float magnitude = std::sqrt(direction.x * direction.x + direction.y * direction.y + direction.z * direction.z);
                    Vector3 normDirection(0.0f, 0.0f, 0.0f);

                    if(magnitude > 0.0001f)
                    {
                        normDirection.x = direction.x / magnitude;
                        normDirection.y = direction.y / magnitude;
                        normDirection.z = direction.z / magnitude;
                    }

                    Move(playerId, normDirection, static_cast<std::int32_t>(BASE_MOVE_SPEED));
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

            if(packet->data().size() >= sizeof(Vector3))
            {
                std::memcpy(&direction.x, packet->data().data(), sizeof(Vector3));
            }

            Shoot(playerId, direction, packet->clienttick());
            break;
        }
        case Protocol::IngameType::Hit: {
            spdlog::info("world(room id) {}: player {} hit", uuids::to_string(_roomId), uuids::to_string(playerId));
            break;
        }
        default: break;
        }
    }
}

void World::UpdateState()
{
    std::lock_guard playerLock(_playerMutex);

    // 60fps 고정 틱에 맞춰 초 단위의 dt(델타 타임)를 산출하고 물리 적용
    constexpr float div = 1'000'000.0f;
    const float dt = static_cast<float>(_tickInterval.count()) / div;

    for(auto& [id, player] : _players)
    {
        if(player)
        {
            player->RecordSnapshot(_tickCount.load());
            player->SimulatePhysics(dt, GRAVITY);
        }
    }
}

bool World::GetPlayerPosition(uuids::uuid playerId, Vector3& outPosition)
{
    std::lock_guard playerLock(_playerMutex);
    auto it = _players.find(playerId);
    if(it == _players.end() || !it->second)
    {
        return false;
    }
    outPosition = it->second->position;
    return true;
}

void World::PrintScoreboard()
{
    std::lock_guard lock(_playerMutex);
    spdlog::info("=========================================");
    spdlog::info("              SCOREBOARD                 ");
    spdlog::info("-----------------------------------------");

    Protocol::ScoreboardPacket scorePacket;
    scorePacket.set_roomid(uuids::to_string(_roomId));
    for(const auto& [id, player] : _players)
    {
        if(player)
        {
            spdlog::info("Player {}: Pos: ({:.2f}, {:.2f}, {:.2f}) | HP: {} | Kills: {} | Deaths: {}",
                         uuids::to_string(id).substr(0, 8), player->position.x, player->position.y, player->position.z,
                         player->hp, player->kill, player->death);

            auto* score = scorePacket.add_scores();
            score->set_playerid(uuids::to_string(id));
            score->set_kill(player->kill);
            score->set_death(player->death);
            score->set_heal(player->heal);
        }
    }

    spdlog::info("=========================================");

    std::string serializedScoreboard;
    if(!scorePacket.SerializeToString(&serializedScoreboard))
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
    if(!ingamePacket.SerializeToString(&serializedIngamePacket))
    {
        spdlog::error("world(room id) {}: failed to serialize ingamePacket", uuids::to_string(_roomId));
        return;
    }

    auto sendPacket = NetworkPacketPool::GetInstance()->Rent();
    sendPacket->set_type(Protocol::PacketType::Ingame);
    sendPacket->set_data(serializedIngamePacket);

    for(const auto& [id, weakSession] : _sessions)
    {
        if(auto session = weakSession.lock())
        {
            if(session->IsValid())
                session->EnqueueTcpSendPacket(sendPacket);
        }
    }
}