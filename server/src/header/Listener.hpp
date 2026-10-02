#pragma once

#include <asio.hpp>
#include "ServerPolicy.hpp"
#include <memory>
#include <spdlog/spdlog.h>
#include <unordered_map>
#include <uuid.h>
#include <vector>
#include <queue>
#include <deque>
#include <atomic>
#include <mutex>

#include "Base.hpp"
#include "NetworkGateway.hpp"
#include "Packet.pb.h"
#include "CustomUtility.hpp"

using namespace Protocol;

class ExecutionContext;
class Room;
class Session;

class Listener : public IBase, public NetworkGateway
{
private:
    struct SecretKey
    {
    };

public:
    explicit Listener(SecretKey, std::shared_ptr<ExecutionContext> ioManager, std::uint16_t tcpPort,
                      std::uint16_t udpPort, const std::vector<std::string> allowedPlayers);

    ~Listener() override
    {
        spdlog::info("server successfully destroyed");
    }

    static std::shared_ptr<Listener> Create(std::shared_ptr<ExecutionContext> ioManager, std::uint16_t tcpPort,
                                            std::uint16_t udpPort, const std::vector<std::string> allowedPlayers)
    {
        auto newServer = std::make_shared<Listener>(SecretKey{}, ioManager, tcpPort, udpPort, allowedPlayers);
        return newServer;
    }

public:
    void Start() override;
    void Stop() override;

    // 네트워크 연결 함수
    void AcceptAsync();

    // 룸 설정
    void SetDedicatedRoom(std::shared_ptr<Room> room);
    std::shared_ptr<Room> GetDedicatedRoom() const;

    void PostSend(OutboundMessage message) override;
    void ConnectionReady(const std::shared_ptr<Session> &session, asio::ip::udp::endpoint ep);
    void StopInput();
    bool IsDrained();

    // TCP InfoHandshake 시 클라이언트가 전달한 userId(AuthServer 발급) 검증 및 세션 등록
    bool RegisterAuthenticatedSession(const std::string &userId, const std::shared_ptr<Session> &session);

private:
    using Raw = std::vector<unsigned char>;
    void EnqueueSendData(asio::ip::udp::endpoint ep, std::shared_ptr<const Raw> networkBuffer);
    void EnqueueSendDataOnStrand(asio::ip::udp::endpoint ep, std::shared_ptr<const Raw> networkBuffer,
                                 std::optional<StateKey> stateKey = {},
                                 std::chrono::steady_clock::time_point submitted = std::chrono::steady_clock::now());
    void SendAsyncByUdp();
    void DrainOutbound();
    void SendOnStrand(const OutboundMessage &message);
    void ReceiveAsyncByUdp();
    void ProcessPacket(std::shared_ptr<asio::ip::udp::endpoint> sender, std::uint16_t size, const unsigned char *data);

    bool IsPlayerAllowed(const std::string &userId) const
    {
        return _allowedPlayers.contains(userId);
    }

private:
    std::shared_ptr<ExecutionContext> _ioManager;
    asio::io_context::strand _strand;

    asio::ip::tcp::endpoint _tcpEndpoint;
    asio::ip::tcp::acceptor _acceptor;

    asio::ip::udp::socket _udpSocket;
    asio::ip::udp::endpoint _udpEndpoint;

    // 단일 룸 정보
    std::shared_ptr<Room> _dedicatedRoom;
    mutable std::mutex _dedicatedRoomMutex;
    std::unordered_set<std::string> _allowedPlayers;

    // 인증 전 임시 연결 세션 및 인증 완료된 세션 정보
    std::unordered_set<std::shared_ptr<Session>> _pendingSessions;
    std::unordered_map<uuids::uuid, std::shared_ptr<Session>> _sessions;
    std::mutex _sessionsMutex;

    struct UdpSend
    {
        asio::ip::udp::endpoint endpoint;
        std::shared_ptr<const Raw> sendBuffer;
        std::optional<StateKey> stateKey;
        std::chrono::steady_clock::time_point submitted;
    };

    std::deque<UdpSend> _networkBufferQueue;
    std::chrono::steady_clock::time_point _receiveWindow = std::chrono::steady_clock::now();
    std::size_t _receiveCount = 0;
    std::size_t _udpInFlight = 0;
    bool _accepting = true;
    bool _closing = false;

    struct Connection
    {
        Recipient recipient;
        asio::ip::udp::endpoint endpoint;
        std::weak_ptr<Session> session;
        std::chrono::steady_clock::time_point window = std::chrono::steady_clock::now();
        std::size_t inputCount = 0;
    };

    std::unordered_map<uuids::uuid, Connection> _connections;
    std::mutex _outboundMutex;
    std::deque<OutboundMessage> _outboundQueue;
    bool _outboundScheduled = false;
    std::atomic<std::size_t> _pendingOutbound{0};
    std::atomic<std::size_t> _pendingTcp{0};
};