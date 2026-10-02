#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace ServerPolicy
{
inline constexpr std::size_t MaximumPlayers = 12;
inline constexpr std::size_t PendingConnections = 24;
inline constexpr std::size_t PendingOutbound = 1024;
inline constexpr std::size_t PendingGameInputs = 1024;
inline constexpr std::size_t WorldInputs = 1024;
inline constexpr std::size_t TcpProcessing = 64;
inline constexpr std::size_t TcpSends = 256;
inline constexpr std::size_t UdpSends = 2048;
inline constexpr std::size_t MinimumUdpInFlight = 1;
inline constexpr int OutboundBatch = 8;
inline constexpr std::size_t GlobalUdpRate = 4096;
inline constexpr std::size_t ConnectionInputRate = 240;
inline constexpr std::size_t MaximumInboundBytes = 8192;
inline constexpr auto RateWindow = std::chrono::seconds(1);
inline constexpr auto HandshakeTimeout = std::chrono::seconds(10);
inline constexpr auto HeartbeatInterval = std::chrono::seconds(2);
inline constexpr auto HeartbeatTimeout = std::chrono::seconds(10);
inline constexpr auto ReportTimeout = std::chrono::seconds(3);
inline constexpr auto NetworkDrainTimeout = std::chrono::seconds(2);
inline constexpr auto DrainPollInterval = std::chrono::milliseconds(5);
inline constexpr std::size_t HttpResponseBytes = 65536;
inline constexpr std::size_t MinimumWorkers = 1;
inline constexpr std::size_t GameWorkers = 1;
inline constexpr std::size_t BlockingWorkers = 4;
inline constexpr std::size_t PoolCapacity = 500;
inline constexpr std::uint16_t AuthServerPort = 18080;
inline constexpr std::size_t MetricsSamples = 256;
inline constexpr auto TickInterval = std::chrono::microseconds(16666);
inline constexpr int MaximumCatchupTicks = 5;
inline constexpr std::size_t ScoreboardTickInterval = 60;
} // namespace ServerPolicy

namespace NetworkFraming
{
inline constexpr std::size_t LengthHeaderBytes = sizeof(std::uint16_t);
inline constexpr std::size_t MaximumTcpPayloadBytes = std::numeric_limits<std::uint16_t>::max();
inline constexpr std::size_t MaximumUdpDatagramBytes = 65507;
inline constexpr std::size_t MaximumUdpPayloadBytes = MaximumUdpDatagramBytes - LengthHeaderBytes;
inline constexpr std::size_t UdpReceiveBufferBytes = std::numeric_limits<std::uint16_t>::max();
} // namespace NetworkFraming
