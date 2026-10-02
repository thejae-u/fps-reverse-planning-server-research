#pragma once

#include <asio.hpp>
#include "ServerPolicy.hpp"
#include <memory>
#include <unordered_map>
#include <nlohmann/json.hpp>
#include "ExecutionContext.hpp"
#include "BlockingExecutor.hpp"
#include "NetworkGateway.hpp"
#include "World.hpp"

using namespace Protocol;
using json = nlohmann::json;

class Room : public std::enable_shared_from_this<Room>
{
private:
    struct SecretKey
    {
    };

public:
    explicit Room(SecretKey, std::shared_ptr<ExecutionContext>, std::shared_ptr<BlockingExecutor>, uuids::uuid,
                  const std::string &, std::size_t);
    using ShutdownCallback = std::function<void()>;

    static std::shared_ptr<Room> Create(std::shared_ptr<ExecutionContext> game,
                                        std::shared_ptr<BlockingExecutor> blocking, uuids::uuid matchId,
                                        const std::string &token, std::size_t expected)
    {
        return std::make_shared<Room>(SecretKey{}, std::move(game), std::move(blocking), matchId, token, expected);
    }

    void SetShutdownCallback(ShutdownCallback callback)
    {
        _shutdownCallback = std::move(callback);
    }

    void SetGateway(std::weak_ptr<NetworkGateway> gateway)
    {
        _gateway = std::move(gateway);
    }

    void PostInput(std::string payload, Recipient sender);
    void PostHandshake(Recipient recipient, std::int32_t preset);
    void AddSession(uuids::uuid id, Participant participant);
    void RemoveSession(uuids::uuid id, std::uint64_t generation);
    void EnqueuePacket(std::shared_ptr<IngamePacket> packet, Recipient sender);
    // Called only on the shared Room/World game strand.
    void OnMatchFinished();
    void Broadcast(PacketType type, std::string payload, Transport transport = Transport::Udp,
                   std::optional<StateKey> stateKey = {}) const;
    void SendTo(Recipient recipient, PacketType type, std::string payload, Transport transport) const;
    void Stop(); // Main lifecycle thread only; waits for the game barrier.

    World *GetWorld() const
    {
        return _world.get();
    }

    uuids::uuid GetId() const
    {
        return _matchId;
    }

private:
    std::shared_ptr<ExecutionContext> _game;
    std::shared_ptr<BlockingExecutor> _blocking;
    asio::io_context::strand _strand;
    std::weak_ptr<NetworkGateway> _gateway;
    uuids::uuid _matchId;
    std::string _authToken;
    const std::string _serverHost = "127.0.0.1";
    const std::uint16_t _serverPort = ServerPolicy::AuthServerPort;
    std::size_t _expectedPlayerCount;
    bool _isWorldStarted = false;
    bool _isMatchFinished = false;
    bool _stopped = false;
    // Prevent a producer from posting after the Game executor drain barrier.
    std::mutex _submissionMutex;
    bool _acceptingPosts = true;
    std::atomic<std::size_t> _pendingInputs{0};
    ShutdownCallback _shutdownCallback;
    std::unordered_map<uuids::uuid, Participant> _sessions;
    std::unique_ptr<World> _world;
};