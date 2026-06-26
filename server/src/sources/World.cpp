#include "World.hpp"
#include "Session.hpp"
#include "Room.hpp"

void World::Init(const std::unordered_map<uuids::uuid, std::weak_ptr<Session>>& sessions)
{
    std::lock_guard<std::mutex> playerLock(_playerMutex);
    _playerSize = sessions.size();

    for(const auto [id, session] : sessions)
    {
        auto newPlayer = std::make_unique<Player>();
        _players.insert({ id, std::move(newPlayer) });
    }

    // some init logics: team division, role assignment

    spdlog::info("world(room id) {}: created", uuids::to_string(_roomId));
}

void World::Move(uuids::uuid playerId, Vector3 direction, std::int32_t speed)
{
    std::lock_guard<std::mutex> playerLock(_playerMutex);
    if(!_players.contains(playerId))
    {
        spdlog::info("world(room id) {}: no player {}", uuids::to_string(_roomId), uuids::to_string(playerId));
        return;
    }

    // Validation 1: Speed check (no negative speed, cap at MAX_SPEED)
    if (speed < 0)
    {
        speed = 0;
    }
    else if (speed > MAX_SPEED)
    {
        speed = MAX_SPEED;
    }

    // Validation 2: Direction vector components constraint [-1, 1]
    if (std::abs(direction.x) > 1 || std::abs(direction.y) > 1 || std::abs(direction.z) > 1)
    {
        spdlog::warn("world(room id) {}: player {} sent invalid direction ({}, {}, {})",
            uuids::to_string(_roomId), uuids::to_string(playerId), direction.x, direction.y, direction.z);
        return;
    }

    _players[playerId]->velocity = direction * speed;
}

void World::Jump(uuids::uuid player)
{
}

void World::Shoot(uuids::uuid player, Vector3 direction)
{
}

void World::StartUpdate(std::weak_ptr<Room> weakRoom, const std::chrono::microseconds interval)
{
    if (_isUpdating.exchange(true))
        return; // Already updating

    _weakRoom = weakRoom;
    _tickInterval = interval;
    ScheduleNextTick();
    spdlog::info("world(room id) {}: update loop started with interval {}ms", uuids::to_string(_roomId), _tickInterval.count());
}

void World::StopUpdate()
{
    if (!_isUpdating.exchange(false))
        return; // Not updating

    _timer.cancel();
    spdlog::info("world(room id) {}: update loop stopped", uuids::to_string(_roomId));
}

void World::EnqueuePacket(std::shared_ptr<Protocol::IngamePacket> packet)
{
    std::lock_guard<std::mutex> lock(_queueMutex);
    _packetQueue.push(packet);
}

void World::ScheduleNextTick()
{
    if (!_isUpdating)
        return;

    _timer.expires_at(_timer.expiry() + _tickInterval);
    _timer.async_wait([weakRoom = _weakRoom, roomId = _roomId](const std::error_code& ec) {
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
    auto start = std::chrono::high_resolution_clock::now();

    // 1. Process queued inputs from clients
    ProcessQueue();

    // 2. Update world/physics state
    UpdateState();

    // 3. Broadcast updated player states to all clients in the room
    auto room = _weakRoom.lock();
    if (room)
    {
        std::lock_guard<std::mutex> playerLock(_playerMutex);
        for (const auto& [id, player] : _players)
        {
            if (!player)
                continue;

            auto ingamePacket = std::make_shared<Protocol::IngamePacket>();
            ingamePacket->set_sessionid(uuids::to_string(id));
            ingamePacket->set_roomid(uuids::to_string(_roomId));
            ingamePacket->set_method(Protocol::IngameType::Move);

            // Serialize Vector3 position to bytes data
            std::string posData(sizeof(Vector3), '\0');
            std::memcpy(&posData[0], &player->position, sizeof(Vector3));
            ingamePacket->set_data(posData);

            std::string sendBuffer;
            if (ingamePacket->SerializeToString(&sendBuffer))
            {
                auto sendPacket = std::make_shared<Protocol::Packet>();
                sendPacket->set_type(Protocol::PacketType::Ingame);
                sendPacket->set_data(sendBuffer);
                room->Broadcast(std::move(sendPacket));
            }
        }
    }

    auto end = std::chrono::high_resolution_clock::now();
    auto elapsedUs = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
    spdlog::info("Update End in {} ms, {}", elapsedUs, _tickCount.load());
    ++_tickCount;
    
    {
        std::lock_guard<std::mutex> lock(_metricsMutex);
        _tickDurationsUs.push_back(elapsedUs);
    }
}

void World::ProcessQueue()
{
    std::queue<std::shared_ptr<Protocol::IngamePacket>> localQueue;
    {
        std::lock_guard<std::mutex> lock(_queueMutex);
        std::swap(localQueue, _packetQueue);
    }

    while(!localQueue.empty())
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
            case Protocol::IngameType::Move:
            {
                if (packet->data().size() >= sizeof(std::int32_t) * 4)
                {
                    Vector3 direction;
                    std::int32_t speed;
                    std::memcpy(&direction.x, packet->data().data(), sizeof(std::int32_t));
                    std::memcpy(&direction.y, packet->data().data() + sizeof(std::int32_t), sizeof(std::int32_t));
                    std::memcpy(&direction.z, packet->data().data() + sizeof(std::int32_t) * 2, sizeof(std::int32_t));
                    std::memcpy(&speed, packet->data().data() + sizeof(std::int32_t) * 3, sizeof(std::int32_t));
                    Move(playerId, direction, speed);
                }
                break;
            }
            case Protocol::IngameType::Jump:
            {
                spdlog::info("world(room id) {}: player {} jump", uuids::to_string(_roomId), uuids::to_string(playerId));
                break;
            }
            case Protocol::IngameType::Shoot:
            {
                spdlog::info("world(room id) {}: player {} shoot", uuids::to_string(_roomId), uuids::to_string(playerId));
                break;
            }
            case Protocol::IngameType::Hit:
            {
                spdlog::info("world(room id) {}: player {} hit", uuids::to_string(_roomId), uuids::to_string(playerId));
                break;
            }
            default:
                break;
        }
    }
}

void World::UpdateState()
{
    std::lock_guard<std::mutex> playerLock(_playerMutex);
    
    // 수정 부분: 60fps 고정 틱에 맞춰 초 단위의 dt(델타 타임)를 산출하고 중력을 적용
    float dt = static_cast<float>(_tickInterval.count()) / 1000000.0f;
    
    // Apply velocity to position as a basic tick update step
    for(auto& [id, player] : _players)
    {
        if (player)
        {
            // 수정 부분: 공중에 있는 플레이어에게 고정 틱 기반 중력(9.8 * dt) 가속도 누적
            if (!player->isGrounded)
            {
                player->velocity.y -= GRAVITY * dt;
            }
            player->position += player->velocity;
        }
    }
}

bool World::GetPlayerPosition(uuids::uuid playerId, Vector3& outPosition)
{
    std::lock_guard<std::mutex> playerLock(_playerMutex);
    auto it = _players.find(playerId);
    if(it == _players.end() || !it->second)
    {
        return false;
    }
    outPosition = it->second->position;
    return true;
}

void World::ClearMetrics()
{
    std::lock_guard<std::mutex> lock(_metricsMutex);
    _tickDurationsUs.clear();
}

void World::GetMetrics(std::int64_t& minUs, std::int64_t& maxUs, double& avgUs)
{
    std::lock_guard<std::mutex> lock(_metricsMutex);
    if (_tickDurationsUs.empty())
    {
        minUs = maxUs = 0;
        avgUs = 0.0;
        return;
    }
    minUs = *std::ranges::min_element(_tickDurationsUs);
    maxUs = *std::ranges::max_element(_tickDurationsUs);
    std::int64_t sum = 0;
    for (auto val : _tickDurationsUs)
    {
        sum += val;
    }
    avgUs = static_cast<double>(sum) / _tickDurationsUs.size();
}
