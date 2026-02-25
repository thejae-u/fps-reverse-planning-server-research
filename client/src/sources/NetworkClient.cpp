#include "NetworkClient.hpp"

NetworkClient::NetworkClient() : _socket(std::make_shared<asio::ip::tcp::socket>(_ioContext)) {
}

NetworkClient::~NetworkClient() {
    Disconnect();
}

void NetworkClient::Connect(const std::string& host, uint16_t port) {
    if (_connected) return;

    try {
        asio::ip::tcp::resolver resolver(_ioContext);
        auto endpoints = resolver.resolve(host, std::to_string(port));

        asio::async_connect(*_socket, endpoints, [this](std::error_code ec, asio::ip::tcp::endpoint endpoint) {
            if (!ec) {
                _connected = true;
                AddLog("Connected to " + endpoint.address().to_string() + ":" + std::to_string(endpoint.port()));
            } else {
                AddLog("Connection failed: " + ec.message(), spdlog::level::err);
            }
        });

        _contextThread = std::make_unique<std::thread>([this]() {
            try {
                asio::io_context::work work(_ioContext);
                _ioContext.run();
            } catch (std::exception& e) {
                AddLog(std::string("IO Context error: ") + e.what(), spdlog::level::err);
            }
        });
    } catch (std::exception& e) {
        AddLog(std::string("Connect error: ") + e.what(), spdlog::level::err);
    }
}

void NetworkClient::Disconnect() {
    _connected = false;
    _ioContext.stop();
    if (_contextThread && _contextThread->joinable()) {
        _contextThread->join();
    }
    _ioContext.reset();
    
    std::error_code ec;
    _socket->close(ec);
    
    // Create a new socket for next connection
    _socket = std::make_shared<asio::ip::tcp::socket>(_ioContext);
    
    AddLog("Disconnected");
}

void NetworkClient::AddLog(const std::string& msg, spdlog::level::level_enum level) {
    std::lock_guard<std::mutex> lock(_logMutex);
    _logs.push_back({msg, level});
    if (_logs.size() > 100) {
        _logs.pop_front();
    }
    spdlog::log(level, msg);
}
