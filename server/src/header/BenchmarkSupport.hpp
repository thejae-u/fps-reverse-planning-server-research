#pragma once

#include <chrono>
#include <cstdlib>
#include <string_view>
#include "Packet.pb.h"

// 벤치마크 측정 로직을 일반 입력 처리와 분리. 기본 실행에서는 활성화되지 않음.
namespace BenchmarkSupport
{
using Clock = std::chrono::steady_clock;

inline bool Enabled()
{
    static const bool enabled = [] {
        const auto value = std::getenv("SERVER_BENCHMARK");
        return value && std::string_view(value) == "1";
    }();
    return enabled;
}

inline std::uint64_t NonnegativeMicroseconds(Clock::duration duration)
{
    return duration > Clock::duration::zero()
               ? static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(duration).count())
               : 0;
}

inline std::uint64_t Microseconds(Clock::duration duration)
{
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(duration).count());
}

struct Sample
{
    Clock::time_point submitted;
    Clock::time_point queued;
    Protocol::BenchmarkAckPacket reply;
};
} // namespace BenchmarkSupport
