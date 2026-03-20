#pragma once

#include <asio.hpp>
#include <functional>
#include <queue>
#include <memory>
#include <mutex>
#include <condition_variable>
#include <spdlog/spdlog.h>
#include <uuid.h>

#include "IOManager.hpp"
#include "Packet.pb.h"

using namespace Protocol;

class Session : public std::enable_shared_from_this<Session>
{
private:
    struct SecretKey {};

public:
    explicit Session(SecretKey, std::shared_ptr<IOManager> ioManager, uuids::uuid sessionId, std::uint16_t udpPort)
    : _ioManager(ioManager), _socketPtr(std::make_shared<asio::ip::tcp::socket>(ioManager->GetIoContext())), _serverUdpPort(udpPort), _clientUdpPort(0),
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
    void Start();
    void Stop();

    void StartHandShaking();

    void SetRoom(uuids::uuid roomId);

    uuids::uuid GetId() const { return _id; }
    uuids::uuid GetRoomId() const { return _roomId; }

    using NotifyDisconnectCallback = std::function<void(const std::shared_ptr<Session>&)>;
    void SetNotifyDisconnectCallback(NotifyDisconnectCallback callback);

private:
    std::shared_ptr<IOManager> _ioManager;
    std::shared_ptr<asio::ip::tcp::socket> _socketPtr;
    std::uint16_t _serverUdpPort;
    std::uint16_t _clientUdpPort;

    // Set by first handshaking
    uuids::uuid _id;
    uuids::uuid _roomId;

    std::uint32_t _readSize;
    std::uint32_t _readNetSize;
    const std::uint16_t MAX_BUF_SIZE = 65535;
    std::vector<unsigned char> _readBuffer;

    NotifyDisconnectCallback _disconnectCallback;

    std::queue<std::shared_ptr<Packet>> _sendQueue;
    std::mutex _sendQueueMutex;
    std::condition_variable _sendQueueCv;

public:
    void EnqueueSendPacket(const std::shared_ptr<Packet> data);
    void DequeueSendPacket();

private:
    void SendAsync(const std::shared_ptr<Packet> data);
    void ReadSizeAsync();
    void ReadDataAsync(const std::uint16_t& dataSize);

    // Handshaking Functions
    void ExchangeUdpPort();
};