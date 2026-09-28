#pragma once

#include <asio.hpp>
#include <memory>
#include <spdlog/spdlog.h>
#include <unordered_map>
#include <uuid.h>
#include <vector>
#include <queue>
#include <atomic>
#include <mutex>

#include "Base.hpp"
#include "Packet.pb.h"
#include "CustomUtility.hpp"
using namespace Protocol;

class IOManager;
class Room;
class Session;

constexpr std::uint16_t BUF_SIZE = 65535;

class Listener : public IBase
{
private:
    struct SecretKey
    {
    };

public:
    explicit Listener(SecretKey, std::shared_ptr<IOManager> ioManager, std::uint16_t tcpPort, std::uint16_t udpPort, const std::vector<std::string> allowedPlayers);
    ~Listener() override { spdlog::info("server successfully destroyed"); }

    static std::shared_ptr<Listener> Create(std::shared_ptr<IOManager> ioManager, std::uint16_t tcpPort, std::uint16_t udpPort, const std::vector<std::string> allowedPlayers)
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
    void SetDedicatedRoom(std::shared_ptr<Room> room) { _dedicatedRoom = room; }
    std::shared_ptr<Room> GetDedicatedRoom() const { return _dedicatedRoom; }

    // TCP InfoHandshake 시 클라이언트가 전달한 userId(AuthServer 발급) 검증 및 세션 등록
    bool RegisterAuthenticatedSession(const std::string& userId, const std::shared_ptr<Session>& session);

private:
    using Raw = std::vector<unsigned char>;
    void EnqueueSendData(asio::ip::udp::endpoint ep, std::shared_ptr<Raw> networkBuffer);
    void SendAsyncByUdp();
    void ReceiveAsyncByUdp();
    void ProcessPacket(std::shared_ptr<asio::ip::udp::endpoint> sender, std::uint16_t size, const unsigned char* data);
    bool IsPlayerAllowed(const std::string& userId) const
    {
        return _allowedPlayers.contains(userId);
    }

private:
    std::shared_ptr<IOManager> _ioManager;
    asio::io_context::strand _strand;

    asio::ip::tcp::endpoint _tcpEndpoint;
    asio::ip::tcp::acceptor _acceptor;

    asio::ip::udp::socket _udpSocket;
    asio::ip::udp::endpoint _udpEndpoint;

    // 단일 룸 정보
    std::shared_ptr<Room> _dedicatedRoom;
    std::unordered_set<std::string> _allowedPlayers;

    // 인증 전 임시 연결 세션 및 인증 완료된 세션 정보
    std::unordered_set<std::shared_ptr<Session>> _pendingSessions;
    std::unordered_map<uuids::uuid, std::shared_ptr<Session>> _sessions;
    std::mutex _sessionsMutex;

    std::queue<std::pair<asio::ip::udp::endpoint, std::shared_ptr<Raw>>> _networkBufferQueue;
    std::queue<std::pair<asio::ip::udp::endpoint, std::shared_ptr<Raw>>> _noLockNetworkBufferQueue;
    std::mutex _networkBufferQueueMutex;
    std::atomic<bool> _isSending{ false };
};