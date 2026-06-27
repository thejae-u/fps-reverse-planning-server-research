#pragma once
#include <queue>
#include <mutex>
#include <memory>
#include <new>

#include "Packet.pb.h"

class IngamePacketPool : public std::enable_shared_from_this<IngamePacketPool>
{
private:
    struct SecretKey
    {
    };

    inline static std::shared_ptr<IngamePacketPool> _instance = nullptr;
    inline static std::mutex _initMtx;

public:
    explicit IngamePacketPool(SecretKey, const std::size_t maxSize)
        : _maxSize(maxSize)
    {
    }

    static void Init(const std::size_t maxSize)
    {
        std::lock_guard<std::mutex> lock(_initMtx);
        if(_instance == nullptr)
            _instance = std::make_shared<IngamePacketPool>(SecretKey{}, maxSize);
    }

    static std::shared_ptr<IngamePacketPool> GetInstance()
    {
        return _instance;
    }

    std::shared_ptr<Protocol::IngamePacket> Rent()
    {
        Node* oldHead = _head.load(std::memory_order_acquire);
        while(oldHead && !_head.compare_exchange_weak(
                  oldHead,
                  oldHead->next,
                  std::memory_order_release,
                  std::memory_order_acquire))
        {
        }

        std::unique_ptr<Protocol::IngamePacket> packet;
        if(oldHead == nullptr)
        {
            packet = std::make_unique<Protocol::IngamePacket>();
        }
        else
        {
            packet = std::move(oldHead->packet);
            delete oldHead;
            _poolSize.fetch_sub(1, std::memory_order_relaxed);
        }

        auto self(shared_from_this());
        Protocol::IngamePacket* raw = packet.release(); // 임시 소유권 해제 (for rent)
        return std::shared_ptr<Protocol::IngamePacket>(raw, [self, raw](Protocol::IngamePacket*) {
            self->Return(std::unique_ptr<Protocol::IngamePacket>(raw));
        });
    }

private:
    void Return(std::unique_ptr<Protocol::IngamePacket> packet)
    {
        packet->Clear();
        if(_poolSize.load(std::memory_order_relaxed) >= _maxSize)
            return; // 최대 사이즈 초과 시 해제 

        Node* newNode = new Node();
        newNode->packet = std::move(packet);

        Node* oldHead = _head.load(std::memory_order_acquire);

        // CAS Loop: newNode의 next -> oldHead 연결, _head의 next -> newNode
        do
        {
            newNode->next = oldHead;
        } while(!_head.compare_exchange_weak(
            oldHead,
            newNode,
            std::memory_order_release,
            std::memory_order_acquire));

        // add pool size
        _poolSize.fetch_add(1, std::memory_order_relaxed);
    }

private:
    struct Node
    {
        std::unique_ptr<Protocol::IngamePacket> packet;
        Node* next = nullptr;
    };

    alignas(std::hardware_destructive_interference_size) std::atomic<Node*> _head{ nullptr };
    alignas(std::hardware_destructive_interference_size) std::atomic<std::size_t> _poolSize{ 0 };
    const std::size_t _maxSize;
};