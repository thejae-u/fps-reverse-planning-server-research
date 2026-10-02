#pragma once

#include <algorithm>
#include "ServerPolicy.hpp"
#include <chrono>
#include <cstdlib>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>
#include <spdlog/spdlog.h>

// Opt-in bounded windows; no per-packet logging. Set SERVER_METRICS=1.
class RuntimeMetrics
{
public:
    static bool Enabled()
    {
        static const bool enabled = [] {
            const auto value = std::getenv("SERVER_METRICS");
            return value && std::string(value) == "1";
        }();
        return enabled;
    }

    static void Observe(const std::string &name, std::chrono::steady_clock::duration elapsed, std::size_t depth = 0)
    {
        if (!Enabled())
            return;
        static std::mutex mutex;
        static std::unordered_map<std::string, Window> windows;
        std::lock_guard lock(mutex);
        auto &window = windows[name];
        window.samples.push_back(std::chrono::duration_cast<std::chrono::microseconds>(elapsed).count());
        window.depth = std::max(window.depth, depth);
        if (window.samples.size() < ServerPolicy::MetricsSamples)
            return;
        std::sort(window.samples.begin(), window.samples.end());
        spdlog::info("[metrics] {} us p50={} p95={} p99={} max_depth={}", name, window.samples[PercentileIndex(50)],
                     window.samples[PercentileIndex(95)], window.samples[PercentileIndex(99)], window.depth);
        window.samples.clear();
        window.depth = 0;
    }

private:
    static constexpr std::size_t PercentileIndex(std::size_t percent)
    {
        return (ServerPolicy::MetricsSamples - 1) * percent / 100;
    }

    struct Window
    {
        std::vector<std::int64_t> samples;
        std::size_t depth = 0;
    };
};