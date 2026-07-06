#pragma once

#include <asio.hpp>
#include <deque>
#include <queue>
#include <functional>
#include <memory>
#include <mutex>
#include <spdlog/spdlog.h>
#include <string>
#include <thread>

#include "Packet.pb.h"

#include <unordered_map>
#include <chrono>

class IOManager;

using namespace Protocol;

class NetworkClient : public std::enable_shared_from_this<NetworkClient>
{
public:
    struct PlayerState
    {
        std::string id;
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
        int hp = 100;
        int kills = 0;
        int deaths = 0;
        std::chrono::steady_clock::time_point lastUpdate;

        bool isShooting = false;
        std::chrono::steady_clock::time_point shootTime;
        float shootOrigin[3] = { 0.0f, 0.0f, 0.0f };
        float shootDir[3] = { 0.0f, 0.0f, 1.0f };

        bool isHit = false;
        std::chrono::steady_clock::time_point hitTime;
    };

    NetworkClient(std::shared_ptr<IOManager> ioManager);
    ~NetworkClient();

    void Connect(const std::string& host, uint16_t port);
    void Disconnect();
    bool IsConnected() const { return _connected; }
    std::uint16_t GetClientUdpPort() const { return _clientUdpPort; }

    const std::string& GetRoomId() const { return _roomId; }
    const std::string& GetSessionId() const { return _sessionId; }
    bool IsMatching() const { return _isMatching; }
    void SetMatching(bool matching) { _isMatching = matching; }
    bool IsIngame() const { return _isIngame; }
    void SetIngame(bool ingame) { _isIngame = ingame; }
    uint64_t GetLastServerTick() const { return _lastServerTick; }

    void SendMatchRequest();
    void Send(const std::string& message);
    void SendIngamePacket(IngameType type, const std::string& data, uint64_t clientTick = 0);
    void SendUdpCorrect(const std::string& message, const std::string& host, uint16_t port);
    void SendUdpMalformed(const std::string& message, const std::string& host, uint16_t port, int errorType);
    void SendUdpHolePunching();

    std::vector<PlayerState> GetRoomPlayers() const;
    void ClearRoomPlayers();

    struct LogMessage
    {
        std::string text;
        spdlog::level::level_enum level;
    };

    std::vector<LogMessage> GetLogs() const
    {
        std::lock_guard<std::mutex> lock(_logMutex);
        return std::vector<LogMessage>(_logs.begin(), _logs.end());
    }

    using MessageCallback = std::function<void(const std::string&)>;
    void SetMessageCallback(MessageCallback callback);
    void SetUdpMessageCallback(MessageCallback callback);

private:
    void AddLog(const std::string& msg, spdlog::level::level_enum level = spdlog::level::info);
    void AsyncRead();
    void AsyncReadUdp();
    void InitUdpSocket();

    void AsyncHandshake();

    std::shared_ptr<IOManager> _ioManager;
    asio::strand<asio::io_context::executor_type> _strand;
    std::shared_ptr<asio::ip::tcp::socket> _socket;
    asio::ip::udp::socket _udpSocket;
    asio::ip::udp::endpoint _udpRemoteEndpoint;

    std::uint16_t _serverUdpPort = 0;
    std::uint16_t _clientUdpPort = 0;

    bool _connected = false;
    bool _isMatching = false;
    bool _isIngame = false;
    uint64_t _lastServerTick = 0;
    std::string _roomId;
    std::string _sessionId;
    std::string _serverHost;
    asio::ip::address _serverAddress;

    std::deque<LogMessage> _logs;
    mutable std::mutex _logMutex;

    std::unordered_map<std::string, PlayerState> _roomPlayers;
    mutable std::mutex _roomPlayersMutex;

    // Read-related members
    uint16_t _readNetSize;
    uint16_t _readSize;
    asio::streambuf _readBuffer;
    MessageCallback _messageCallback;
    MessageCallback _udpMessageCallback;
    std::array<char, 65535> _udpReceiveBuffer;

    // Write-related members
    std::queue<std::shared_ptr<std::vector<char>>> _sendTcpQueue;
    std::mutex _sendTcpQueueMutex;
    std::atomic<bool> _isWriting{ false };

    void DoSendAsyncTcpLoop();
};