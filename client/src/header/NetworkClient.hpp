#pragma once

#include <asio.hpp>
#include <string>
#include <memory>
#include <deque>
#include <mutex>
#include <thread>
#include <spdlog/spdlog.h>
#include <functional>

class NetworkClient {
public:
    NetworkClient();
    ~NetworkClient();

    void Connect(const std::string& host, uint16_t port);
    void Disconnect();
    bool IsConnected() const { return _connected; }

    void Send(const std::string& message);

    struct LogMessage {
        std::string text;
        spdlog::level::level_enum level;
    };

    const std::deque<LogMessage>& GetLogs() const { 
        std::lock_guard<std::mutex> lock(_logMutex);
        return _logs; 
    }

    using MessageCallback = std::function<void(const std::string&)>;
    void SetMessageCallback(MessageCallback callback);

private:
    void AddLog(const std::string& msg, spdlog::level::level_enum level = spdlog::level::info);
    void AsyncRead();

    asio::io_context _ioContext;
    std::shared_ptr<asio::ip::tcp::socket> _socket;
    std::unique_ptr<std::thread> _contextThread;

    bool _connected = false;
    std::deque<LogMessage> _logs;
    mutable std::mutex _logMutex;

    // Read-related members
    uint32_t _readSize;
    asio::streambuf _readBuffer;
    MessageCallback _messageCallback;
};
