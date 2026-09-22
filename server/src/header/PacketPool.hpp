#pragma once
#include <queue>
#include <mutex>
#include <memory>
#include <new>
#include <cassert>

#include "Packet.pb.h"

template <typename T>
class ObjectPool : public std::enable_shared_from_this<ObjectPool<T>>
{
private:
    struct SecretKey {};
    inline static std::shared_ptr<ObjectPool<T>> _instance = nullptr;
    inline static std::mutex _initMutex;

public:
    explicit ObjectPool(SecretKey, const std::size_t maxSize) : _maxSize(maxSize) {}
    ~ObjectPool()
    {
        Node* current = _head.load(std::memory_order_acquire);
        while(current != nullptr)
        {
            Node* nextNode = current->next;
            delete current;
            current = nextNode;
        }
        _head.store(nullptr, std::memory_order_relaxed);
        _poolSize.store(0, std::memory_order_relaxed);
    }

    static void Init(const std::size_t maxSize)
    {
        std::lock_guard lock(_initMutex);
        if(_instance == nullptr)
            _instance = std::make_shared<ObjectPool<T>>(SecretKey{}, maxSize);
        spdlog::info("ObjectPool<{}> init complete (maxSize : {})", typeid(T).name(), maxSize);
    }

    static void Release()
    {
        std::lock_guard lock(_initMutex);
        if(_instance != nullptr)
        {
            spdlog::info("ObjectPool<{}> release requested", typeid(T).name());
            _instance = nullptr;
        }
    }

    static std::shared_ptr<ObjectPool<T>> GetInstance()
    {
        assert(_instance != nullptr && "ObjectPool has NOT been initialized. Call Init() first");
        return _instance;
    }

    std::shared_ptr<T> Rent()
    {
        Node* oldHead = _head.load(std::memory_order_acquire);
        while(oldHead && !_head.compare_exchange_weak(
                         oldHead,
                         oldHead->next,
                         std::memory_order_release,
                         std::memory_order_acquire))
        {
        }

        std::unique_ptr<T> item;
        if(oldHead == nullptr)
        {
            item = std::make_unique<T>();
        }
        else
        {
            item = std::move(oldHead->item);
            delete oldHead;
            _poolSize.fetch_sub(1, std::memory_order_relaxed);
        }

        auto self(this->shared_from_this());
        T* raw = item.release();
        return std::shared_ptr<T>(raw, [self, raw](T*) {
            self->Return(std::unique_ptr<T>(raw));
        });
    }

private:
    void Return(std::unique_ptr<T> item)
    {
        // C++20 스마트 리셋
        if constexpr(requires(T& obj) { obj.Clear(); })
        {
            item->Clear(); // Protobuf message
        }
        else if constexpr(requires(T& obj) { obj.clear(); })
        {
            item->clear(); // std::vector, std::string 등
        }

        // 최대 크기 초과 -> 자동 소멸
        if(_poolSize.load(std::memory_order_relaxed) >= _maxSize)
            return;

        Node* newNode = new Node();
        newNode->item = std::move(item);

        Node* oldHead = _head.load(std::memory_order_acquire);
        do
        {
            newNode->next = oldHead;
        } while(!_head.compare_exchange_weak(
            oldHead,
            newNode,
            std::memory_order_release,
            std::memory_order_acquire));

        _poolSize.fetch_add(1, std::memory_order_relaxed);
    }

private:
    struct Node {
        std::unique_ptr<T> item;
        Node* next = nullptr;
    };

    alignas(std::hardware_destructive_interference_size) std::atomic<Node*> _head{ nullptr };
    alignas(std::hardware_destructive_interference_size) std::atomic<std::size_t> _poolSize{ 0 };
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