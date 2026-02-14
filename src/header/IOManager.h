#ifndef _H_IO_MANAGER_
#define _H_IO_MANAGER_

#include <iostream>

#include <asio.hpp>
#include <vector>
#include <memory>
#include <thread>

class IOManager
{
private:
	struct SecretKey {};
public:
	explicit IOManager(SecretKey, std::size_t threadCount) : _guard(asio::make_work_guard(_io)), _threadCount(threadCount)
	{
	}

	static std::shared_ptr<IOManager> Create(std::size_t threadCount)
	{
		auto newIOManager = std::make_shared<IOManager>(SecretKey{}, threadCount);
		for (auto i = 0; i < threadCount; ++i)
		{
			newIOManager->_workers.emplace_back([newIOManager]() { newIOManager->_io.run(); });
		}

		return newIOManager;
	}

	template <typename CompletionHandler>
	auto RegisterWork(CompletionHandler&& handler)
	{
		return asio::post(_io, std::forward<CompletionHandler>(handler));
	}

	void Stop()
	{
		_io.stop();
		_guard.reset();

		for (auto& w : _workers)
		{
			if (w.joinable())
				w.join();
		}
	}

private:
	asio::io_context _io;
	asio::executor_work_guard<asio::io_context::executor_type> _guard;
	std::vector<std::thread> _workers;
	std::size_t _threadCount;
};

#endif