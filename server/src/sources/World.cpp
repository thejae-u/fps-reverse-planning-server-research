#include "World.hpp"
#include "Session.hpp"

void World::Init(const std::vector<uuids::uuid> sessionIds)
{
    std::lock_guard<std::mutex> playerLock(_playerMutex);
    _playerSize = sessionIds.size();

    for(const auto id : sessionIds)
    {
        auto newPlayer = std::make_unique<Player>();
        _players.insert({ id, std::move(newPlayer) });
    }

    // some init logics: team division, role assignment

    spdlog::info("world(room id) {}: created", uuids::to_string(_roomId));
}

void World::Move(uuids::uuid playerId, Vector3 position)
{
    std::lock_guard<std::mutex> playerLock(_playerMutex);
    if(_players.find(playerId) == _players.end())
    {
        spdlog::info("world(room id) {}: no player {}", uuids::to_string(_roomId), uuids::to_string(playerId));
        return;
    }

    _players[playerId]->position += position;
}
