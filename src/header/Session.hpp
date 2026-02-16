#pragma once

#include <asio.hpp>
#include <memory>
#include <spdlog/spdlog.h>
#include <uuid.h>

class Session : public std::enable_shared_from_this<Session>
{
private:
    struct SecretKey {};

public:
    explicit Session(SecretKey, asio::io_context& io, uuids::uuid sessionId) : _socketPtr(std::make_shared<asio::ip::tcp::socket>(io)), _id(sessionId)
    {
        spdlog::info("empty session created");
    }

    ~Session() { spdlog::warn("Session destroyed: {}", uuids::to_string(_id)); }

    static std::shared_ptr<Session> Create(asio::io_context& io, uuids::uuid sessionId)
    {
        auto newSession = std::make_shared<Session>(SecretKey{}, io, sessionId);
        return newSession;
    }

public:
    std::shared_ptr<asio::ip::tcp::socket> GetSocket() { return _socketPtr; }

    asio::ip::tcp::endpoint GetEndpoint() { return _socketPtr->remote_endpoint(); }
    void Start();
    void Stop();

    void SetRoom();

    uuids::uuid GetId() const { return _id; }
    uuids::uuid GetRoomId() const { return _roomId; }

private:
    std::shared_ptr<asio::ip::tcp::socket> _socketPtr;

    // Set by first handshaking
    uuids::uuid _id;
    uuids::uuid _roomId;
};