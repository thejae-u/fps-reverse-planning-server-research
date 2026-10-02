#pragma once

#include <vector>
#include "ServerPolicy.hpp"
#include <mutex>
#include <memory>
#include <new>
#include <cassert>

#include "Packet.pb.h"

template <typename T> class ObjectPool : public std::enable_shared_from_this<ObjectPool<T>>
{
private:
    struct SecretKey
    {
    };

    inline static std::shared_ptr<ObjectPool<T>> _instance = nullptr;
    inline static std::mutex _initMutex;

public:
    explicit ObjectPool(SecretKey, const std::size_t maxSize)
        : _maxSize(maxSize)
    {
        _items.reserve(maxSize);
    }

    static void Init(const std::size_t maxSize)
    {
        std::lock_guard lock(_initMutex);
        if (_instance == nullptr)
            _instance = std::make_shared<ObjectPool<T>>(SecretKey{}, maxSize);
        spdlog::info("ObjectPool<{}> init complete (maxSize : {})", typeid(T).name(), maxSize);
    }

    static void Release()
    {
        std::lock_guard lock(_initMutex);
        if (_instance != nullptr)
        {
            spdlog::info("ObjectPool<{}> release requested", typeid(T).name());
            _instance = nullptr;
        }
    }

    static std::shared_ptr<ObjectPool<T>> GetInstance()
    {
        std::lock_guard lock(_initMutex);
        assert(_instance != nullptr && "ObjectPool has NOT been initialized. Call Init() first");
        return _instance;
    }

    std::shared_ptr<T> Rent()
    {
        std::unique_ptr<T> item;
        {
            std::lock_guard lock(_poolMutex);
            if (!_items.empty())
            {
                item = std::move(_items.back());
                _items.pop_back();
            }
        }
        if (!item)
            item = std::make_unique<T>();

        auto self(this->shared_from_this());
        T *raw = item.release();
        return std::shared_ptr<T>(raw, [self, raw](T *) {
            self->Return(std::unique_ptr<T>(raw));
        });
    }

private:
    void Return(std::unique_ptr<T> item)
    {
        // C++20 스마트 리셋
        if constexpr (requires(T &obj) { obj.Clear(); })
        {
            item->Clear(); // Protobuf message
        }
        else if constexpr (requires(T &obj) { obj.clear(); })
        {
            item->clear(); // std::vector, std::string 등
        }

        std::lock_guard lock(_poolMutex);
        if (_items.size() < _maxSize)
            _items.push_back(std::move(item));
    }

private:
    // CAS는 Node 수명을 보장하지 않는다. 기존 즉시 삭제는 경쟁 Rent에 UAF/ABA를 만든다.
    // 안전한 회수 기법 없이 복원하지 않는다. 생성/Clear는 잠금 밖에서 수행한다.
    // 생성 시 reserve하여 반환 잠금 구간의 vector 재할당도 피한다.
    std::mutex _poolMutex;
    std::vector<std::unique_ptr<T>> _items;
    const std::size_t _maxSize;
};

namespace Protocol
{
class IngamePacket;
class NetworkPacket;
} // namespace Protocol

using IngamePacketPool = ObjectPool<Protocol::IngamePacket>;
using NetworkPacketPool = ObjectPool<Protocol::NetworkPacket>;
using ByteBufferPool = ObjectPool<std::vector<unsigned char>>;