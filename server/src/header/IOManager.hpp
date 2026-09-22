#pragma once

#include <asio.hpp>
#include <iostream>
#include <memory>
#include <spdlog/spdlog.h>
#include <thread>
#include <vector>
#include <atomic>
#include <utility>
#include <string>

class IOManager : public std::enable_shared_from_this<IOManager>
{
private:
    struct SecretKey {};

public:
    explicit IOManager(SecretKey, std::string name, std::size_t blockingThreadCount)
        : _name(std::move(name)),
          _ioContext(std::make_shared<asio::io_context>()),
          _blockingPool(blockingThreadCount),
          _workGuard(asio::make_work_guard(*_ioContext))
    {
        spdlog::info("io manager {} created", _name);
    }

    ~IOManager()
    {
        Stop();
        spdlog::info("io manager {} destroyed", _name);
    }

    static std::shared_ptr<IOManager> Create(std::string name, std::size_t threadCount, std::size_t blockingThreadCount)
    {
        auto newIOManager = std::make_shared<IOManager>(SecretKey{}, std::move(name), blockingThreadCount);
        
        auto io = newIOManager->_ioContext;
        for (std::size_t i = 0; i < threadCount; ++i)
        {
            newIOManager->_workers.emplace_back([io]() {
                try {
                    io->run();
                } catch (const std::exception& e) {
                    spdlog::error("IO worker exception: {}", e.what());
                }
            });
        }

        return newIOManager;
    }

public:
    template <typename CompletionHandler>
    auto PostOnIOContext(CompletionHandler&& handler)
    {
        return asio::post(*_ioContext, std::forward<CompletionHandler>(handler));
    }

    template <typename CompletionHandler>
    auto PostOnBlockingPool(CompletionHandler&& handler)
    {
        return asio::post(_blockingPool, std::forward<CompletionHandler>(handler));
    }

    void Stop()
    {
        if (_stopped.exchange(true))
            return;

        _workGuard.reset();
        if (_ioContext)
            _ioContext->stop();

        for (auto& w : _workers)
        {
            if (w.joinable())
                w.join();
        }
        _workers.clear();

        spdlog::info("io manager {} stop complete", _name);
    }

public:
    asio::io_context& GetIoContext() { return *_ioContext; }
    asio::thread_pool& GetBlockingPool() { return _blockingPool; }

private:
    std::string _name;
    std::shared_ptr<asio::io_context> _ioContext;
    asio::thread_pool _blockingPool;

    asio::executor_work_guard<asio::io_context::executor_type> _workGuard;
    std::vector<std::thread> _workers;
    std::atomic<bool> _stopped{ false };
};