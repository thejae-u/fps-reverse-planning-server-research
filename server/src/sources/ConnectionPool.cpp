#include "ConnectionPool.hpp"

#include "WebServerClient.hpp"

void ConnectionPool::Start()
{
    spdlog::info("internal connection pool started on 9000...");
    AcceptWebServerClientAsync();
}

void ConnectionPool::Stop()
{
}

void ConnectionPool::AcceptWebServerClientAsync()
{
    auto newPool = std::make_shared<WebServerClient>(); // 새 Connection Client 생성
    newPool->Init(_poolIdCount++, _ioManager->GetIoContext()); // 관리 ID 부여 및 소켓 객체 생성

    _acceptor.async_accept(*newPool->GetSocket(), [weakSelf = GetWeak<ConnectionPool>(), newPool](const std::error_code& ec) {
        if(auto self = weakSelf.lock())
        {
            if(ec)
            {
                spdlog::error("connection pool acceptor: {}", ec.message());
                self->AcceptWebServerClientAsync();
                return;
            }

            spdlog::info("connection pool acceptor: newPool {} successfully connected", newPool->GetId());
            self->_pool[newPool->GetId()] = std::move(newPool);

            self->AcceptWebServerClientAsync();
        }
    });
}

std::shared_ptr<WebServerClient> ConnectionPool::Rent()
{
    std::lock_guard<std::mutex> lock(_poolMutex);
    if (_pool.empty())
    {
        return nullptr;
    }

    for(const auto& [id, client] : _pool)
    {
        if(!client->IsInUse() && client->IsValid())
            return client;
    }

    return nullptr;
}

void ConnectionPool::Return(std::shared_ptr<WebServerClient> client)
{
    std::lock_guard<std::mutex> lock(_poolMutex);
    if (!client->IsValid())
    {
        _pool.erase(client->GetId()); // 소멸하도록 풀에서 제거
        return;
    }

    _pool[client->GetId()]->Return(); // 대여 상태 초기화
}

void ConnectionPool::NotifyExpired(std::shared_ptr<WebServerClient> client)
{
    auto id = client->GetId();
    
    std::lock_guard<std::mutex> lock(_poolMutex);
    _pool.erase(id);
    
    spdlog::warn("internal connection pool notify expired: {}", id);
}
