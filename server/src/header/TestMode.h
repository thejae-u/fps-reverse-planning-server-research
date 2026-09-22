#pragma once

#include <spdlog/spdlog.h>
#include <asio.hpp>

class TestMode
{
public:
    explicit TestMode(const std::uint16_t testClientCount) : _testClientCount(testClientCount)
    {
    }
    
public:
    void RunTestMode();
    void RunSimulation();
    
private:
    std::uint16_t _testClientCount;
};
