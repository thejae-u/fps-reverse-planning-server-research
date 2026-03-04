#pragma once

#include <asio.hpp>
#include <iostream>
#include <memory>
#include <spdlog/spdlog.h>
#include <thread>
#include <vector>

class IOManager
{
private:
    struct SecretKey {};

public:
    explicit IOManager(SecretKey, std::string name, std::size_t threadCount)
    : _name(name), _guard(asio::make_work_guard(_io)), _threadCount(threadCount)
    {
        spdlog::info("io manager {} created", _name);
    }

    ~IOManager() { spdlog::info("io manager {} destroyed", _name); }

    static std::shared_ptr<IOManager> Create(std::string name, std::size_t threadCount)
    {
        auto newIOManager = std::make_shared<IOManager>(SecretKey{}, name, threadCount);
        for(auto i = 0; i < threadCount; ++i)
        {
            newIOManager->_workers.emplace_back(std::make_shared<std::thread>([newIOManager]() { newIOManager->_io.run(); }));
        }

        return newIOManager;
    }

public:
    template <typename CompletionHandler>
    auto RegisterWork(CompletionHandler&& handler)
    {
        return asio::post(_io, std::forward<CompletionHandler>(handler));
    }

    void Stop()
    {
        _io.stop();
        _guard.reset();

        for(auto& w : _workers)
        {
            if(w->joinable())
                w->join();
        }

        spdlog::info("io manager stop complete\n");
    }

public:
    asio::io_context& GetIoContext() { return _io; }

private:
    std::string _name;
    asio::io_context _io;
    asio::executor_work_guard<asio::io_context::executor_type> _guard;
    std::vector<std::shared_ptr<std::thread>> _workers;
    std::size_t _threadCount;
};