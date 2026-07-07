// server/src/header/pch.h
#pragma once

// 1. C++ Standard Library Headers
#include <iostream>
#include <memory>
#include <string>
#include <vector>
#include <thread>
#include <chrono>
#include <mutex>
#include <queue>
#include <map>
#include <unordered_map>
#include <unordered_set>
#include <atomic>
#include <new>
#include <limits>
#include <type_traits>
#include <utility>

// 2. Third-Party Library Headers
#include <asio.hpp>
#include <spdlog/spdlog.h>
#include <uuid.h>

// 3. Custom Headers
#include "Packet.pb.h"
#include "Internal.pb.h"
#include "CustomUtility.hpp"