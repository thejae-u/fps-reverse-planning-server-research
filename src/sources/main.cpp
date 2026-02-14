#include <iostream>
#include <thread>

#include "IOManager.h"
#include "Server.h"

int main()
{
	auto threadCount = std::thread::hardware_concurrency() * 2;
	auto ioManager = IOManager::Create(threadCount);

	ioManager->Stop();
	return 0;
}
