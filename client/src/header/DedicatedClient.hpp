#pragma once

#include <asio.hpp>
#include <memory>
#include <mutex>
#include <string>
#include <vector>
#include <array>
#include <queue>
#include <atomic>
#include <functional>
#include <spdlog/spdlog.h>

#include "Packet.pb.h"
#include "IOManager.hpp"

class DedicatedClient : public std::enable_shared_from_this<DedicatedClient>
{
public:
    DedicatedClient(std::shared_ptr<IOManager> ioManager);
    ~DedicatedClient();

    void Connect(const std::string& host, uint16_t port);
    void Disconnect();

    bool IsConnected() const { return _connected; }
    bool IsHandshaked() const { return _handshaked; }
    bool IsIngame() const { return _isIngame; }

    std::string GetSessionId() const { return _sessionId; }
    std::string GetRoomId() const { return _roomId; }
    uint16_t GetServerUdpPort() const { return _serverUdpPort; }
    uint16_t GetClientUdpPort() const { return _clientUdpPort; }

    void SendIngamePacket(Protocol::IngameType type, const std::string& data);

    // Random input streaming
    void StartRandomInput(int intervalMs = 50);
    void StopRandomInput();
    bool IsRandomInputActive() const { return _randomInputActive; }
    uint32_t GetRandomPacketsSent() const { return _randomPacketsSent.load(); }

    // Event callbacks
    void SetOnIngameReady(std::function<void()> cb) { _onIngameReady = cb; }
    void SetOnDisconnected(std::function<void(const std::string& reason)> cb) { _onDisconnected = cb; }

    std::vector<std::string> ConsumeLogs();

private:
    void ScheduleRandomInput(int intervalMs);
    void SendRandomInputPacket();
    void AddLog(const std::string& msg);
    void InitUdpSocket();
    void AsyncReadTcp();
    void AsyncReadUdp();
    void AsyncHandshake();
    void SendUdpHolePunching();
    void SendTcp(const std::shared_ptr<Protocol::NetworkPacket>& packet);
    void DoSendTcp();

    std::shared_ptr<IOManager> _ioManager;
    asio::strand<asio::io_context::executor_type> _strand;

    std::shared_ptr<asio::ip::tcp::socket> _tcpSocket;
    asio::ip::udp::socket _udpSocket;
    asio::ip::address _serverAddress;
    std::string _serverHost;

    std::atomic<bool> _connected{ false };
    std::atomic<bool> _handshaked{ false };
    std::atomic<bool> _isIngame{ false };

    std::string _sessionId;
    std::string _roomId;
    uint16_t _serverUdpPort = 0;
    uint16_t _clientUdpPort = 0;

    std::vector<unsigned char> _tcpReadBuffer;
    std::array<uint8_t, 65536> _udpReadBuffer{};
    asio::ip::udp::endpoint _udpSenderEndpoint;

    std::queue<std::shared_ptr<std::vector<unsigned char>>> _sendTcpQueue;
    std::mutex _sendTcpQueueMutex;
    std::atomic<bool> _isWriting{ false };

    std::vector<std::string> _logMessages;
    mutable std::mutex _logMutex;

    std::function<void()> _onIngameReady;
    std::function<void(const std::string& reason)> _onDisconnected;

    std::shared_ptr<asio::steady_timer> _randomInputTimer;
    std::atomic<bool> _randomInputActive{ false };
    std::atomic<uint32_t> _randomPacketsSent{ 0 };
};
