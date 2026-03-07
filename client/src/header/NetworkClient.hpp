#pragma once

#include <asio.hpp>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <spdlog/spdlog.h>
#include <string>
#include <thread>

class NetworkClient
{
public:
    NetworkClient();
    ~NetworkClient();

    void Connect(const std::string& host, uint16_t port);
    void Disconnect();
    bool IsConnected() const { return _connected; }

    void Send(const std::string& message);
    void SendUdpCorrect(const std::string& message, const std::string& host, uint16_t port);
    void SendUdpMalformed(const std::string& message, const std::string& host, uint16_t port, int errorType);

    struct LogMessage {
        std::string text;
        spdlog::level::level_enum level;
    };

    const std::deque<LogMessage>& GetLogs() const
    {
        std::lock_guard<std::mutex> lock(_logMutex);
        return _logs;
    }

    using MessageCallback = std::function<void(const std::string&)>;
    void SetMessageCallback(MessageCallback callback);
    void SetUdpMessageCallback(MessageCallback callback);

private:
    void AddLog(const std::string& msg, spdlog::level::level_enum level = spdlog::level::info);
    void AsyncRead();
    void AsyncReadUdp();
    void EnsureIOThreadStarted();

    asio::io_context _ioContext;
    std::shared_ptr<asio::ip::tcp::socket> _socket;
    asio::ip::udp::socket _udpSocket;
    asio::ip::udp::endpoint _udpRemoteEndpoint;
    std::unique_ptr<std::thread> _contextThread;

    bool _connected = false;
    std::deque<LogMessage> _logs;
    mutable std::mutex _logMutex;

    // Read-related members
    uint32_t _readSize;
    asio::streambuf _readBuffer;
    MessageCallback _messageCallback;
    MessageCallback _udpMessageCallback;
    std::array<char, 65535> _udpReceiveBuffer;
};
