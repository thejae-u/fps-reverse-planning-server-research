#include "Session.hpp"

Session::Session(SecretKey, asio::io_context& io) : _socketPtr(std::make_shared<asio::ip::tcp::socket>(io))
{
    spdlog::info("empty session created");
}

Session::~Session() { spdlog::warn("Session destroyed"); }