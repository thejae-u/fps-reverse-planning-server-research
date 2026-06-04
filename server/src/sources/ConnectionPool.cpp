#include "ConnectionPool.hpp"

#include "WebServerClient.hpp"

void ConnectionPool::Start()
{
    spdlog::info("internal connection pool started on 9000...");
    AcceptWebServerClientAsync();
    
    spdlog::info("start connection pool cleanup timer...");
    StartCleanupTimer();
}

void ConnectionPool::Stop()
{
    _cleanupTimer.cancel();
    
    if(_acceptor.is_open())
        _acceptor.close();
    
    std::lock_guard<std::mutex> lock(_poolMutex);
    for(auto& [id, client] : _pool)
    {
        if(client->IsValid())
            client->GetSocket()->close();
    }
    
    _pool.clear();
}

void ConnectionPool::AcceptWebServerClientAsync()
{
    auto newPool = std::make_shared<WebServerClient>(_poolIdCount++, _ioManager->GetIoContext(), GetWeak<ConnectionPool>()); // 새 Connection Client 생성
    _acceptor.async_accept(*newPool->GetSocket(), [weakSelf = GetWeak<ConnectionPool>(), newPool](const std::error_code& ec) {
        if(auto self = weakSelf.lock())
        {
            if(ec)
            {
                if(ec == asio::error::operation_aborted)
                {
                    spdlog::info("connection pool acceptor successfully released");
                    return;
                }
                
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

void ConnectionPool::StartCleanupTimer()
{
    _cleanupTimer.expires_after(std::chrono::minutes(1));
    _cleanupTimer.async_wait([weakSelf = GetWeak<ConnectionPool>()](const std::error_code& ec) {
        if(auto self = weakSelf.lock())
        {
            if(ec)
            {
                if(ec == asio::error::operation_aborted)
                {
                    spdlog::info("connection pool cleanup timer successfully stopped");
                    return;
                }
                
                spdlog::error("connection pool cleanup timer error: {}", ec.message());
                spdlog::info("restart new cleanup timer...");
                self->StartCleanupTimer();
                return;
            }

            const auto now= std::chrono::steady_clock::now();
            std::lock_guard<std::mutex> lock(self->_poolMutex);
            
            if(self->_pool.size() <= self->_minPoolSize)
            {
                self->StartCleanupTimer();
                return;
            }
            
            for(auto it = self->_pool.begin(); it != self->_pool.end();)
            {
                if(auto client = it->second; !client->IsInUse() && now - client->GetLastActivityTime() > self->_idleTimeout)
                {
                    spdlog::info("cleanup idle client: {}", client->GetId());
                    it = self->_pool.erase(it); // erase -> next iteration returned
                }
                else // 지우지 않았다면 그대로 증가
                    ++it;
            }
            
            self->StartCleanupTimer();
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
