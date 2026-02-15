#pragma once

#include <asio.hpp>
#include <memory>
#include <spdlog/spdlog.h>

class Session : public std::enable_shared_from_this<Session>
{
private:
    struct SecretKey {};

public:
    explicit Session(SecretKey, asio::io_context& io);
    ~Session();

    static std::shared_ptr<Session> Create(asio::io_context& io)
    {
        auto newSession = std::make_shared<Session>(SecretKey{}, io);
        return newSession;
    }

public:
    std::shared_ptr<asio::ip::tcp::socket> GetSocket() { return _socketPtr; }

    asio::ip::tcp::endpoint GetEndpoint() { return _socketPtr->remote_endpoint(); }
    void Start();
    void Stop();

    std::string GetId() { return _id; }
    std::string GetRoomId() { return _roomId; }

private:
    std::shared_ptr<asio::ip::tcp::socket> _socketPtr;

    // Set by first handshaking
    std::string _id;
    std::string _roomId;
};