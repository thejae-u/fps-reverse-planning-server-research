#pragma once

#include <asio.hpp>
#include "ServerPolicy.hpp"
#include <algorithm>
#include <atomic>
#include <memory>
#include <thread>
#include <vector>
#include <string>
#include <spdlog/spdlog.h>

// Stop/Drain must be called by the lifecycle owner, outside these workers.
class ExecutionContext
{
public:
    static std::shared_ptr<ExecutionContext> Create(std::string name, std::size_t count)
    {
        auto context = std::shared_ptr<ExecutionContext>(new ExecutionContext(std::move(name)));
        for (std::size_t i = 0; i < std::max<std::size_t>(ServerPolicy::MinimumWorkers, count); ++i)
            context->_workers.emplace_back([io = context->_io] {
                while (true)
                {
                    try
                    {
                        io->run();
                        break;
                    }
                    catch (const std::exception &e)
                    {
                        spdlog::error("executor handler: {}", e.what());
                    }
                }
            });
        return context;
    }

    ~ExecutionContext()
    {
        Stop();
    }

    asio::io_context &GetIoContext()
    {
        return *_io;
    }

    template <class Handler> void Post(Handler &&handler)
    {
        asio::post(*_io, std::forward<Handler>(handler));
    }

    void Drain()
    {
        Finish(false);
    }

    void Stop()
    {
        Finish(true);
    }

private:
    explicit ExecutionContext(std::string name)
        : _name(std::move(name)), _io(std::make_shared<asio::io_context>()), _guard(asio::make_work_guard(*_io))
    {
    }

    void Finish(bool stop)
    {
        if (_finished.exchange(true))
            return;
        _guard.reset();
        if (stop)
            _io->stop();
        for (auto &worker : _workers)
            if (worker.joinable())
                worker.join();
    }

    std::string _name;
    std::shared_ptr<asio::io_context> _io;
    asio::executor_work_guard<asio::io_context::executor_type> _guard;
    std::vector<std::thread> _workers;
    std::atomic<bool> _finished{false};
};