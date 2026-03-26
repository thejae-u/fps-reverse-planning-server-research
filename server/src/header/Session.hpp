#pragma once

#include <asio.hpp>
#include <functional>
#include <queue>
#include <vector>
#include <memory>
#include <mutex>
#include <atomic>
#include <condition_variable>
#include <spdlog/spdlog.h>
#include <uuid.h>
#include <asio.hpp>

#include "IOManager.hpp"
#include "Packet.pb.h"

using namespace Protocol;

class Session : public std::enable_shared_from_this<Session>
{
private:
    using Raw = std::vector<unsigned char>;
    struct SecretKey {};

public:
    explicit Session(SecretKey, std::shared_ptr<IOManager> ioManager, uuids::uuid sessionId, std::uint16_t udpPort)
    : _ioManager(ioManager), _socketPtr(std::make_shared<asio::ip::tcp::socket>(ioManager->GetIoContext())), _strand(ioManager->GetIoContext()),
      _serverUdpPort(udpPort), _clientUdpPort(0), _isValid(false),
      _id(sessionId), _readSize(0), _readNetSize(0) {}

    ~Session() { spdlog::info("session destroyed: {}", uuids::to_string(_id)); }

    static std::shared_ptr<Session> Create(std::shared_ptr<IOManager> ioManager, uuids::uuid sessionId, std::uint16_t udpPort)
    {
        auto newSession = std::make_shared<Session>(SecretKey{}, ioManager, sessionId, udpPort);
        return newSession;
    }

public:
    std::shared_ptr<asio::ip::tcp::socket> GetSocket() { return _socketPtr; }

    asio::ip::tcp::endpoint GetEndpoint() { return _socketPtr->remote_endpoint(); }
    void StartTcpRead();
    void Stop();

    void Init();
    void PunchUdpHole(asio::ip::udp::endpoint ep) { _clientUdpEp = ep; }

    bool IsValid() const { return _isValid; }

    void SetRoomAndSendInfo(uuids::uuid roomId);

    uuids::uuid GetId() const { return _id; }
    uuids::uuid GetRoomId() const { return _roomId; }

    using NotifyDisconnectCallback = std::function<void(const std::shared_ptr<Session>&)>;
    void AddDisconnectListener(NotifyDisconnectCallback callback);

    using SendToHandler = std::function<void(asio::ip::udp::endpoint, std::shared_ptr<Raw>)>;
    void SetSendToHandler(SendToHandler handler);

private:
    std::shared_ptr<IOManager> _ioManager;
    std::shared_ptr<asio::ip::tcp::socket> _socketPtr;
    asio::io_context::strand _strand;
    std::uint16_t _serverUdpPort;
    std::uint16_t _clientUdpPort;

    asio::ip::udp::endpoint _clientUdpEp;
    std::atomic<bool> _isValid;

    // Set by first handshaking
    uuids::uuid _id;
    uuids::uuid _roomId;

    std::uint16_t _readSize;
    std::uint16_t _readNetSize;
    const std::uint16_t MAX_BUF_SIZE = 65535;
    std::vector<unsigned char> _readBuffer;

    NotifyDisconnectCallback _disconnectCallback;
    std::vector<NotifyDisconnectCallback> _disconnectCallbacks;
    std::mutex _disconnectCallbacksMutex;

    SendToHandler _sendTo;

    std::queue<std::shared_ptr<Raw>> _sendUdpQueue;
    std::mutex _sendUdpQueueMutex;

    std::queue<std::shared_ptr<Raw>> _sendTcpQueue;
    std::mutex _sendTcpQueueMutex;
    std::atomic<bool> _isWriting;

public:
    void EnqueueUdpSendPacket(const std::shared_ptr<Packet> data);
    void EnqueueTcpSendPacket(const std::shared_ptr<Packet> data);

private:
    // Tcp Async Send Data
    void DoSendAsyncTcpLoop();
    void SendAsync(const std::shared_ptr<Packet> data);

    // TCP Async Read data
    void ReadSizeAsync();
    void ReadDataAsync(const std::uint16_t& dataSize);

    // Init Functions
    void SendSessionInfo();
};