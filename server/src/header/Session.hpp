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
#include "ExecutionContext.hpp"
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
    explicit Session(SecretKey, std::shared_ptr<ExecutionContext> ioManager, std::weak_ptr<Listener> listener)
        : _ioManager(ioManager), _socketPtr(std::make_shared<asio::ip::tcp::socket>(ioManager->GetIoContext())),
          _strand(ioManager->GetIoContext()), _weakListener(listener), _handshakeTimer(ioManager->GetIoContext()),
          _heartbeatTimer(ioManager->GetIoContext()), _isValid(false), _state(SessionState::Initializing), _id{},
          _readNetSize(0), _isWriting(false), _isProcessing(false)
    {
    }

    ~Session() override
    {
        spdlog::info("session destroyed: {}", uuids::to_string(GetId()));
    }

    static std::shared_ptr<Session> Create(std::shared_ptr<ExecutionContext> ioManager,
                                           std::weak_ptr<Listener> listener)
    {
        auto newSession = std::make_shared<Session>(SecretKey{}, ioManager, listener);
        return newSession;
    }

  public:
    std::shared_ptr<asio::ip::tcp::socket> GetSocket()
    {
        return _socketPtr;
    }

    asio::ip::tcp::endpoint GetEndpoint() const
    {
        return _socketPtr->remote_endpoint();
    }

    void StartTcpRead();
    void Start() override;
    void Stop() override;

    void Init();
    void PunchUdpHole(const asio::ip::udp::endpoint &ep);
    void ProcessEndGame();

    bool IsValid() const
    {
        return _isValid;
    }

    bool IsStopping() const
    {
        return _stopping.load();
    }

    std::uint64_t GetGeneration() const
    {
        return _generation;
    }

    uuids::uuid GetId() const
    {
        std::lock_guard lock(_identityMutex);
        return _id;
    }

    void SetId(const uuids::uuid &id)
    {
        std::lock_guard lock(_identityMutex);
        _id = id;
    }

    std::int32_t GetPresetId() const
    {
        return _presetId.load();
    }

    void SetPresetId(std::int32_t presetId)
    {
        _presetId.store(presetId);
    }

    using NotifyDisconnectCallback = std::function<void(const std::shared_ptr<Session> &)>;
    void AddDisconnectCallback(NotifyDisconnectCallback callback);

    using SendToHandler = std::function<void(asio::ip::udp::endpoint, std::shared_ptr<const Raw>)>;
    void SetSendToHandler(SendToHandler handler);

  private:
    std::shared_ptr<ExecutionContext> _ioManager;
    std::shared_ptr<asio::ip::tcp::socket> _socketPtr;
    asio::io_context::strand _strand;

    std::weak_ptr<Listener> _weakListener;
    inline static std::atomic<std::uint64_t> _nextGeneration{0};
    const std::uint64_t _generation{++_nextGeneration};

    asio::steady_timer _handshakeTimer;
    asio::steady_timer _heartbeatTimer;
    std::uint64_t _heartbeatSequence{0};
    std::string _heartbeatPayload;
    std::chrono::steady_clock::time_point _heartbeatRequestedAt;
    asio::ip::udp::endpoint _clientUdpEp;
    std::atomic<bool> _isValid;

    std::atomic<SessionState> _state;

    // Set by InfoHandshake authentication
    uuids::uuid _id;
    std::atomic<std::int32_t> _presetId{0};

    std::uint16_t _readNetSize;

    std::vector<NotifyDisconnectCallback> _disconnectCallbacks;
    std::mutex _disconnectCallbacksMutex;
    SendToHandler _sendTo;

    struct TcpSend
    {
        std::shared_ptr<const Raw> sendBuffer;
        std::function<void()> done;
    };

    std::queue<TcpSend> _sendTcpQueue;
    std::atomic<bool> _stopping{false};
    bool _isWriting{false};

    std::queue<std::shared_ptr<NetworkPacket>> _processQueue;
    std::mutex _processQueueMutex;
    std::atomic<bool> _isProcessing{false};
    mutable std::mutex _identityMutex;

  public:
    void EnqueueUdpSendPacket(const std::shared_ptr<NetworkPacket> &data);
    void EnqueueTcpSendPacket(const std::shared_ptr<NetworkPacket> &data);
    void EnqueueTcpBuffer(std::shared_ptr<const Raw> sendBuffer, std::function<void()> done = {});

  private:
    // Tcp Async Send Data
    void EnqueueUdpSendPacketOnStrand(std::shared_ptr<NetworkPacket> data);
    void EnqueueTcpSendPacketOnStrand(std::shared_ptr<NetworkPacket> data);
    void DoSendAsyncTcpLoop();

    // TCP Async Read data
    void ReadSizeAsync();
    void LogTcpReadFailure(const char *stage, const std::error_code &ec, std::size_t transferred,
                           std::size_t expected) const;
    void ReadDataAsync(const std::uint16_t &dataSize);

    // Process Tcp Data
    void EnqueueProcessPacket(const std::shared_ptr<const Raw> &data, const std::uint16_t size);
    void ProcessPacketAsync();
    void ScheduleHeartbeat();
    void HandlePong(const NetworkPacket &packet);
};
