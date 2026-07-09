#include "World.hpp"
#include "Session.hpp"
#include "Room.hpp"
#include "IngamePacketPool.hpp"

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

        // some init logics: team division, role assignment
    }

    spdlog::info("world(room id) {}: created", uuids::to_string(_roomId));
}

void World::Move(uuids::uuid playerId, Vector3 direction, std::int32_t speed)
{
    std::lock_guard playerLock(_playerMutex);
    if(!_players.contains(playerId))
    {
        spdlog::info("world(room id) {}: no player {}", uuids::to_string(_roomId), uuids::to_string(playerId));
        return;
    }

    // Validation 1: Speed check (no negative speed, cap at MAX_SPEED)
    if(speed < 0)
    {
        speed = 0;
    }
    else if(speed > MAX_SPEED)
    {
        speed = MAX_SPEED;
    }

    // Validation 2: Direction vector components constraint [-1, 1]
    if(std::abs(direction.x) > 1 || std::abs(direction.y) > 1 || std::abs(direction.z) > 1)
    {
        spdlog::warn("world(room id) {}: player {} sent invalid direction ({}, {}, {})",
                     uuids::to_string(_roomId), uuids::to_string(playerId), direction.x, direction.y, direction.z);
        return;
    }

    _players[playerId]->velocity = direction * speed;
}

void World::Jump(uuids::uuid player)
{
    std::lock_guard lock(_playerMutex);
    if(!_players.contains(player))
    {
        spdlog::error("world(room id) {}: player {} is not found.", uuids::to_string(_roomId), uuids::to_string(player));
        return;
    }

    auto& targetPlayer = _players[player];
    if(targetPlayer->isGrounded)
    {
        targetPlayer->velocity.y = JUMP_SPEED;
        targetPlayer->isGrounded = false;
    }
}

void World::Shoot(uuids::uuid shooterId, Vector3 direction, std::size_t targetTick)
{
    std::lock_guard lock(_playerMutex);
    if(!_players.contains(shooterId) || !_players[shooterId])
        return;

    // 1. Rewind Players
    auto backup = RewindPlayersNoLock(shooterId, targetTick);
    Vector3 origin = _players[shooterId]->position;

    // 2. Perform distance-based hit detection
    constexpr float HIT_RADIUS = 3.0f;
    constexpr float HIT_RADIUS_SQ = HIT_RADIUS * HIT_RADIUS;
    float len = std::sqrt(direction.x * direction.x + direction.y * direction.y + direction.z * direction.z);
    Vector3 dirNorm = (len > 0.0f) ? Vector3(direction.x / len, direction.y / len, direction.z / len) : Vector3(0, 0, 1);

    std::unordered_set<uuids::uuid> hitTargetIds;
    for(auto& [targetId, targetPlayer] : _players)
    {
        if(targetId == shooterId || !targetPlayer)
            continue;

        // V = enemy - shooter
        Vector3 v(targetPlayer->position.x - origin.x, targetPlayer->position.y - origin.y, targetPlayer->position.z - origin.z);

        // t = V dot D
        float t = v.x * dirNorm.x + v.y * dirNorm.y + v.z * dirNorm.z;

        // behind pass
        if(t < 0.0f)
            continue;

        // P = O + t * D
        Vector3 p(origin.x + t * dirNorm.x, origin.y + t * dirNorm.y, origin.z + t * dirNorm.z);

        // d^2 = ||P - C||^2
        float distSq = std::pow(p.x - targetPlayer->position.x, 2) +
                       std::pow(p.y - targetPlayer->position.y, 2) +
                       std::pow(p.z - targetPlayer->position.z, 2);

        if(distSq <= HIT_RADIUS_SQ)
        {
            HitNoLock(targetId, 10, shooterId);
            hitTargetIds.insert(targetId);
        }
    }

    if(auto room = _weakRoom.lock())
    {
        Protocol::DebugLagCompPacket debugPacket;
        debugPacket.set_shooterid(uuids::to_string(shooterId));
        debugPacket.set_originx(origin.x);
        debugPacket.set_originy(origin.y);
        debugPacket.set_originz(origin.z);
        debugPacket.set_dirx(dirNorm.x);
        debugPacket.set_diry(dirNorm.y);
        debugPacket.set_dirz(dirNorm.z);

        for(const auto& [targetId, targetPlayer] : _players)
        {
            if(targetId == shooterId || !targetPlayer)
                continue;
            auto* targetMsg = debugPacket.add_targets();
            targetMsg->set_targetid(uuids::to_string(targetId));

            // 현재 위치 (백업에서 복원)
            Vector3 presentPos = backup.at(targetId);
            targetMsg->set_presentx(presentPos.x);
            targetMsg->set_presenty(presentPos.y);
            targetMsg->set_presentz(presentPos.z);
            // 되감겼던 위치 (현재 복원 직전의 targetPlayer->position)
            targetMsg->set_rewoundx(targetPlayer->position.x);
            targetMsg->set_rewoundy(targetPlayer->position.y);
            targetMsg->set_rewoundz(targetPlayer->position.z);

            targetMsg->set_ishit(hitTargetIds.contains(targetId));
        }

        std::string serializedDebug;
        if(debugPacket.SerializeToString(&serializedDebug))
        {
            auto ingamePacket = IngamePacketPool::GetInstance()->Rent();
            ingamePacket->set_sessionid(uuids::to_string(shooterId));
            ingamePacket->set_roomid(uuids::to_string(_roomId));
            ingamePacket->set_method(Protocol::IngameType::DebugLagComp);
            ingamePacket->set_data(serializedDebug);

            std::string serializedIngame;
            if(ingamePacket->SerializeToString(&serializedIngame))
            {
                auto sendPacket = std::make_shared<Protocol::Packet>();
                sendPacket->set_type(Protocol::PacketType::Ingame);
                sendPacket->set_data(serializedIngame);
                room->Broadcast(std::move(sendPacket));
            }
        }
    }

    // 3. Restore Players
    RestorePlayersNoLock(backup);
}

void World::HitNoLock(uuids::uuid hitId, std::int32_t damage, uuids::uuid shooterId)
{
    if(!_players.contains(shooterId) || !_players[shooterId])
    {
        spdlog::warn("world(room id) {}: shooter {} not found on hit", uuids::to_string(_roomId), uuids::to_string(shooterId));
        return;
    }

    if(!_players.contains(hitId) || !_players[hitId])
    {
        spdlog::warn("world(room id) {}: hit target {} not found", uuids::to_string(_roomId), uuids::to_string(hitId));
        return;
    }

    if(damage < 0)
    {
        spdlog::warn("world(room id) {}: invalid damage (damage is negative)", uuids::to_string(_roomId));
        return;
    }

    auto& targetPlayer = _players[hitId];
    targetPlayer->hp -= damage;

    bool isDead = false;
    if(targetPlayer->hp <= 0)
    {
        isDead = true;
        targetPlayer->death++;
        targetPlayer->hp = 100; // reset

        _players[shooterId]->kill++;

        spdlog::info("world {}: player {} killed player {}",
                     uuids::to_string(_roomId), uuids::to_string(shooterId), uuids::to_string(hitId));
    }

    if(auto room = _weakRoom.lock())
    {
        // 1. Create and populate HitPacket protobuf message
        Protocol::HitPacket hitPacket;
        hitPacket.set_hitplayerid(uuids::to_string(hitId));
        hitPacket.set_shooterid(uuids::to_string(shooterId));
        hitPacket.set_currenthp(targetPlayer->hp);
        hitPacket.set_deaths(targetPlayer->death);
        hitPacket.set_isdead(isDead);
        hitPacket.set_damage(damage);

        // 2. Serialize HitPacket
        std::string serializedData;
        if(hitPacket.SerializeToString(&serializedData))
        {
            auto ingamePacket = IngamePacketPool::GetInstance()->Rent();
            ingamePacket->set_sessionid(uuids::to_string(hitId));
            ingamePacket->set_roomid(uuids::to_string(_roomId));
            ingamePacket->set_method(Protocol::IngameType::Hit);
            ingamePacket->set_data(serializedData);

            // 3. Serialize outer IngamePacket and broadcast
            std::string serializedIngame;
            if(ingamePacket->SerializeToString(&serializedIngame))
            {
                auto sendPacket = std::make_shared<Protocol::Packet>();
                sendPacket->set_type(Protocol::PacketType::Ingame);
                sendPacket->set_data(serializedIngame);
                room->Broadcast(std::move(sendPacket));
            }
        }
    }
}

void World::Hit(uuids::uuid hitId, std::int32_t damage, uuids::uuid shooterId)
{
    std::lock_guard lock(_playerMutex);
    HitNoLock(hitId, damage, shooterId);
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
    auto room = _weakRoom.lock();
    if(room)
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
                auto sendPacket = std::make_shared<Protocol::Packet>();
                sendPacket->set_type(Protocol::PacketType::Ingame);
                sendPacket->set_data(sendBuffer);
                room->Broadcast(std::move(sendPacket));
            }
        }
    }

    ++_tickCount;

    if(_tickCount % 60 == 0)
    {
        PrintScoreboard();
    }
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
                            spdlog::warn("world(room id){}: player {} teleport suspected. Dist: {}m, Allowed: {}m",
                                uuids::to_string(playerId), actualDist, maxAllowedDist);
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

    // 60fps 고정 틱에 맞춰 초 단위의 dt(델타 타임)를 산출하고 중력을 적용
    constexpr float div = 1'000'000.0f;
    const float dt = static_cast<float>(_tickInterval.count()) / div;

    for(auto& [id, player] : _players)
    {
        if(player)
        {
            // position history for rewind
            PlayerSnapshot snapshot;
            snapshot.tick = _tickCount.load();
            snapshot.position = player->position;

            player->positionHistory.push_back(snapshot);

            if(player->positionHistory.size() > 60) // 60틱 초과 시 오래된 순 제거
            {
                player->positionHistory.pop_front();
            }

            // 고정 틱 기반 중력(9.8 * dt) 가속도 누적
            if(!player->isGrounded)
            {
                player->velocity.y -= GRAVITY * dt;
            }

            player->position += player->velocity * dt;

            if(player->position.y <= 0.0f)
            {
                player->position.y = 0.0f;
                player->velocity.y = 0.0f;
                player->isGrounded = true;
            }
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

std::unordered_map<uuids::uuid, Vector3> World::RewindPlayersNoLock(uuids::uuid shooterId, std::size_t targetTick)
{
    std::unordered_map<uuids::uuid, Vector3> currentPositionsBackup;
    for(auto& [id, player] : _players)
    {
        if(id == shooterId) // 자기 자신
            continue;

        // backup current position
        currentPositionsBackup[id] = player->position;

        // targetTick에 가장 가까운 스냅샷 find
        Vector3 rewoundPosition = player->position;
        float minDiff = std::numeric_limits<float>::max();

        for(const auto& snapshot : player->positionHistory)
        {
            float diff = std::abs(static_cast<float>(snapshot.tick) - static_cast<float>(targetTick));
            if(diff < minDiff)
            {
                minDiff = diff;
                rewoundPosition = snapshot.position;
            }
        }

        player->position = rewoundPosition;
    }

    return currentPositionsBackup;
}

void World::RestorePlayersNoLock(const std::unordered_map<uuids::uuid, Vector3>& backup)
{
    for(const auto& [id, originPosition] : backup)
    {
        if(_players.contains(id) && _players[id])
        {
            _players[id]->position = originPosition;
        }
    }
}

std::unordered_map<uuids::uuid, Vector3> World::RewindPlayers(uuids::uuid shooterId, std::size_t targetTick)
{
    std::lock_guard lock(_playerMutex);
    return RewindPlayersNoLock(shooterId, targetTick);
}

void World::RestorePlayers(const std::unordered_map<uuids::uuid, Vector3>& backup)
{
    std::lock_guard lock(_playerMutex);
    RestorePlayersNoLock(backup);
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

    Protocol::Packet packet;
    packet.set_type(Protocol::PacketType::Ingame);
    packet.set_data(serializedIngamePacket);

    auto sendPacket = std::make_shared<Packet>(packet);
    for(const auto& [id, weakSession] : _sessions)
    {
        if(auto session = weakSession.lock())
        {
            if(session->IsValid())
                session->EnqueueTcpSendPacket(sendPacket);
        }
    }
}