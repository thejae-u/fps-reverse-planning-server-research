#pragma once

#include <spdlog/spdlog.h>
#include <string>
#include <asio.hpp>

class HttpResultReporter
{
public:
    static bool SendMatchResult(
        const std::string& authServerHost,
        std::uint16_t authServerPort,
        const std::string& jsonPayload,
        const std::string& path = "/internal/finish"
    )
    {
        try
        {
            asio::io_context ioContext;
            asio::ip::tcp::resolver resolver(ioContext);
            auto endpoints = resolver.resolve(authServerHost, std::to_string(authServerPort));

            asio::ip::tcp::socket socket(ioContext);
            asio::connect(socket, endpoints);

            std::string request =
            "POST " + path + " HTTP/1.1\r\n"
            "Host: " + authServerHost + ":" + std::to_string(authServerPort) + "\r\n"
            "Content-Type: application/json\r\n"
            "Content-Length: " + std::to_string(jsonPayload.length()) + "\r\n"
            "Connection: close\r\n\r\n" +
            jsonPayload;
            
            std::error_code ec;
            asio::write(socket, asio::buffer(request), ec);
            
            asio::streambuf response;
            asio::read_until(socket, response, "\r\n");
            
            std::istream responseStream(&response);
            std::string httpVersion;
            unsigned int statusCode;
            responseStream >> httpVersion >> statusCode;
            
            spdlog::info("[HttpReporter] response code: {}", statusCode);
            return (statusCode == 200 || statusCode == 204);
        }
        catch(const std::exception& e)
        {
            spdlog::error("[HttpReporter] failed to send match result: {}", e.what());
            return false;
        }
    }
};