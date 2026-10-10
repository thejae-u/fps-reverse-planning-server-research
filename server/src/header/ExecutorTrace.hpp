#pragma once
#include <asio.hpp>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <mutex>
#include <string>
#include <vector>
#include <utility>

// 별도 진단 모드에서만 활성화. 측정 중 파일 I/O 없이 느린 작업을 제한된 메모리에 저장.
namespace ExecutorTrace {
using Clock = std::chrono::steady_clock;
struct Event { std::string label; long long submitted, started, ended; };
inline long long Micros(Clock::time_point value) {
    return std::chrono::duration_cast<std::chrono::microseconds>(value.time_since_epoch()).count();
}
struct Store {
    std::mutex mutex;
    std::vector<Event> events;
    std::size_t dropped = 0;
    std::string path;
    Store() { if (const char* value = std::getenv("SERVER_EXECUTOR_TRACE")) path = value; }
    ~Store() {
        if (path.empty()) return;
        std::ofstream output(path);
        output << "label,submitted_us,started_us,ended_us,wait_us,execution_us,dropped\n";
        for (const auto& e : events)
            output << e.label << ',' << e.submitted << ',' << e.started << ',' << e.ended
                   << ',' << e.started - e.submitted << ',' << e.ended - e.started << ",0\n";
        output << "trace_summary,0,0,0,0,0," << dropped << '\n';
    }
};
inline Store& Data() { static Store store; return store; }
class Scope {
    const char* label;
    Clock::time_point submitted, started;
    bool enabled;
public:
    Scope(const char* name, Clock::time_point queued)
        : label(name), submitted(queued), started(Data().path.empty() ? Clock::time_point{} : Clock::now()), enabled(!Data().path.empty()) {}
    ~Scope() {
        if (!enabled) return;
        auto ended = Clock::now();
        if (started - submitted < std::chrono::milliseconds(1)
            && ended - started < std::chrono::milliseconds(1)) return;
        auto& store = Data();
        std::lock_guard lock(store.mutex);
        if (store.events.size() >= 20000) { ++store.dropped; return; }
        store.events.push_back({label, Micros(submitted), Micros(started), Micros(ended)});
    }
};
template<class Executor, class Handler>
void Post(const Executor& executor, const char* label, Handler&& handler) {
    if (Data().path.empty()) {
        asio::post(executor, std::forward<Handler>(handler));
        return;
    }
    const auto queued = Clock::now();
    asio::post(executor, [label, queued, task = std::forward<Handler>(handler)]() mutable {
        Scope scope(label, queued);
        task();
    });
}
}
