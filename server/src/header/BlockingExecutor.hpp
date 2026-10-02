#pragma once

#include <asio.hpp>
#include "ServerPolicy.hpp"
#include <algorithm>
#include <atomic>

class BlockingExecutor
{
public:
    explicit BlockingExecutor(std::size_t count)
        : _pool(std::max<std::size_t>(ServerPolicy::MinimumWorkers, count))
    {
    }

    ~BlockingExecutor()
    {
        Join();
    }

    template <class Handler> void Post(Handler &&handler)
    {
        asio::post(_pool, std::forward<Handler>(handler));
    }

    void Join()
    {
        if (!_joined.exchange(true))
            _pool.join();
    }

private:
    asio::thread_pool _pool;
    std::atomic<bool> _joined{false};
};