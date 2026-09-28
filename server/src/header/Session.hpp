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

#include "Base.hpp"
#include "IOManager.hpp"
#include "Packet.pb.h"
#include "CustomUtility.hpp"

class Listener;

using namespace Protocol;

class Session : public IBase
{
private:
    using Raw = std::vector<unsigned char>;

    struct SecretKey
    {
    };

public:
    explicit Session(SecretKey, std::shared_ptr<IOManager> ioManager, std::weak_ptr<Listener> listener)
        : _ioManager(ioManager), _socketPtr(std::make_shared<asio::ip::tcp::socket>(ioManager->GetIoContext())), _strand(ioManager->GetIoContext()),
          _weakListener(listener), _isValid(false), _state(SessionState::Initializing),
          _id{}, _readNetSize(0), _isWriting(false), _isProcessing(false)
    {
    }

    ~Session() override { spdlog::info("session destroyed: {}", uuids::to_string(_id)); }

    static std::shared_ptr<Session> Create(std::shared_ptr<IOManager> ioManager, std::weak_ptr<Listener> listener)
    {
        auto newSession = std::make_shared<Session>(SecretKey{}, ioManager, listener);
        return newSession;
    }

public:
    std::shared_ptr<asio::ip::tcp::socket> GetSocket() { return _socketPtr; }

    asio::ip::tcp::endpoint GetEndpoint() const { return _socketPtr->remote_endpoint(); }
    void StartTcpRead();
    void Start() override;
    void Stop() override;

    void Init();
    void PunchUdpHole(const asio::ip::udp::endpoint& ep);

    bool IsValid() const { return _isValid; }

    uuids::uuid GetId() const { return _id; }
    void SetId(const uuids::uuid& id) { _id = id; }
    std::int32_t GetPresetId() const { return _presetId.load(); }
    void SetPresetId(std::int32_t presetId) { _presetId.store(presetId); }

    using NotifyDisconnectCallback = std::function<void(const std::shared_ptr<Session>&)>;
    void AddDisconnectCallback(NotifyDisconnectCallback callback);

    using SendToHandler = std::function<void(asio::ip::udp::endpoint, std::shared_ptr<Raw>)>;
    void SetSendToHandler(SendToHandler handler);

private:
    std::shared_ptr<IOManager> _ioManager;
    std::shared_ptr<asio::ip::tcp::socket> _socketPtr;
    asio::io_context::strand _strand;

    std::weak_ptr<Listener> _weakListener;

    asio::ip::udp::endpoint _clientUdpEp;
    std::atomic<bool> _isValid;

    std::atomic<SessionState> _state;

    // Set by InfoHandshake authentication
    uuids::uuid _id;
    std::atomic<std::int32_t> _presetId{ 0 };

    std::uint16_t _readNetSize;

    NotifyDisconnectCallback _disconnectCallback;
    SendToHandler _sendTo;

    std::queue<std::shared_ptr<Raw>> _sendTcpQueue;
    std::mutex _sendTcpQueueMutex;
    std::atomic<bool> _isWriting{ false };

    std::queue<std::shared_ptr<NetworkPacket>> _processQueue;
    std::mutex _processQueueMutex;
    std::atomic<bool> _isProcessing{ false };

public:
    void EnqueueUdpSendPacket(const std::shared_ptr<NetworkPacket>& data);
    void EnqueueTcpSendPacket(const std::shared_ptr<NetworkPacket>& data);

private:
    // Tcp Async Send Data
    void DoSendAsyncTcpLoop();

    // TCP Async Read data
    void ReadSizeAsync();
    void ReadDataAsync(const std::uint16_t& dataSize);

    // Process Tcp Data
    void EnqueueProcessPacket(const std::shared_ptr<Raw>& data, const std::uint16_t size);
    void ProcessPacketAsync();
};