#pragma once

#include <spdlog/spdlog.h>
#include "ServerPolicy.hpp"
#include <string>
#include <asio.hpp>

class HttpResultReporter
{
public:
    static constexpr unsigned HttpStatusOk = 200;
    static constexpr unsigned HttpStatusNoContent = 204;

    static bool SendMatchResult(const std::string &authServerHost, std::uint16_t authServerPort,
                                const std::string &jsonPayload, const std::string &authToken,
                                const std::string &path = "/internal/finish")
    {
        try
        {
            asio::io_context ioContext;
            asio::ip::tcp::resolver resolver(ioContext);

            asio::ip::tcp::socket socket(ioContext);

            std::string authHeader = "";
            if (!authToken.empty())
                authHeader = "Authorization: Bearer " + authToken + "\r\n";

            std::string request = "POST " + path +
                                  " HTTP/1.1\r\n"
                                  "Host: " +
                                  authServerHost + ":" + std::to_string(authServerPort) +
                                  "\r\n"
                                  "Content-Type: application/json\r\n"
                                  "Content-Length: " +
                                  std::to_string(jsonPayload.length()) + "\r\n" + authHeader +
                                  "Connection: close\r\n\r\n" + jsonPayload;

            // Run asynchronous operations locally on the blocking worker with one deadline.
            asio::steady_timer deadline(ioContext, ServerPolicy::ReportTimeout);
            asio::streambuf response(ServerPolicy::HttpResponseBytes);
            bool success = false;
            auto finish = [&](bool result) {
                success = result;
                deadline.cancel();
                resolver.cancel();
                std::error_code ignored;
                socket.close(ignored);
            };
            deadline.async_wait([&](const std::error_code &ec) {
                if (!ec)
                    finish(false);
            });
            resolver.async_resolve(
                authServerHost, std::to_string(authServerPort),
                [&](const std::error_code &ec, asio::ip::tcp::resolver::results_type endpoints) {
                    if (ec)
                    {
                        finish(false);
                        return;
                    }
                    asio::async_connect(
                        socket, endpoints, [&](const std::error_code &ec, const asio::ip::tcp::endpoint &) {
                            if (ec)
                            {
                                finish(false);
                                return;
                            }
                            asio::async_write(
                                socket, asio::buffer(request), [&](const std::error_code &ec, std::size_t) {
                                    if (ec)
                                    {
                                        finish(false);
                                        return;
                                    }
                                    asio::async_read_until(
                                        socket, response, "\r\n", [&](const std::error_code &ec, std::size_t) {
                                            if (ec)
                                            {
                                                finish(false);
                                                return;
                                            }
                                            std::istream stream(&response);
                                            std::string version;
                                            unsigned int status = 0;
                                            stream >> version >> status;
                                            finish(version.starts_with("HTTP/") &&
                                                   (status == HttpStatusOk || status == HttpStatusNoContent));
                                        });
                                });
                        });
                });
            ioContext.run();
            return success;
        }
        catch (const std::exception &e)
        {
            spdlog::error("[HttpReporter] failed to send match result: {}", e.what());
            return false;
        }
    }
};