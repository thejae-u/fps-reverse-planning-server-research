#include "ConnectionPool.hpp"

#include "WebServerClient.hpp"

void ConnectionPool::Start()
{
    try
    {
        _acceptor.open(_tcpEndpoint.protocol());
        // 소켓 재사용 설정을 통해 TIME_WAIT 상태의 포트도 즉시 바인딩 가능하게 합니다.
        _acceptor.set_option(asio::ip::tcp::acceptor::reuse_address(true));

        _acceptor.bind(_tcpEndpoint);
        _acceptor.listen();

        spdlog::info("internal connection pool started and bound to {}...", _internalPort);
        AcceptWebServerClientAsync();
        
        // 연결 검증용 InternalTest 호출
        InternalTest();
    }
    catch(const std::exception& e)
    {
        // 포트 바인딩 실패 시 구체적인 예외 원인(Address already in use 등)을 콘솔에 찍습니다.
        spdlog::critical("ConnectionPool failed to bind port {}: {}", _internalPort, e.what());
        return;
    }

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
            self->_pool[newPool->GetId()] = newPool;
            newPool->ReceiveHeaderAsync();

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

            const auto now = std::chrono::steady_clock::now();
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

void ConnectionPool::RentAsync(std::function<void(std::shared_ptr<WebServerClient>)> callback)
{
    std::unique_lock<std::mutex> lock(_poolMutex);

    // 1. 기존 풀에서 가용 클라이언트 검색
    for(const auto& [id, client] : _pool)
    {
        if(!client->IsInUse() && client->IsValid())
        {
            client->Rent();
            lock.unlock();
            if(callback)
                callback(client);
            return;
        }
    }

    // 2. 가용 클라이언트가 없는 경우 신규 생성 및 연결
    auto newClient = std::make_shared<WebServerClient>(_poolIdCount++, _ioManager->GetIoContext(), GetWeak<ConnectionPool>());
    newClient->Rent(); // 대여 상태로 생성

    newClient->ConnectAsync(_authServerHost, _authServerPort, [this, callback, newClient](const std::error_code& ec) {
        if(ec)
        {
            spdlog::error("connection pool: failed to connect new client: {}", ec.message());
            if(callback)
                callback(nullptr);
        }

        {
            std::lock_guard<std::mutex> lock(_poolMutex);
            _pool[newClient->GetId()] = newClient;
        }

        if(callback)
            callback(newClient);
    });
}

void ConnectionPool::Return(std::shared_ptr<WebServerClient> client)
{
    std::lock_guard<std::mutex> lock(_poolMutex);
    if(!client->IsValid())
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

void ConnectionPool::InternalTest()
{
    struct RetryState
    {
        int attempt = 1;
        const int maxRetries = 10;
        const std::chrono::seconds delay = std::chrono::seconds(3);
    };

    auto state = std::make_shared<RetryState>();
    auto timer = std::make_shared<asio::steady_timer>(_ioManager->GetIoContext());

    // 재귀적으로 호출할 비동기 람다 함수 정의
    auto doConnect = std::make_shared<std::function<void()>>();
    *doConnect = [this, state, timer, doConnect]() {
        auto socket = std::make_shared<asio::ip::tcp::socket>(_ioManager->GetIoContext());
        auto endpoint = asio::ip::tcp::endpoint(asio::ip::make_address(_authServerHost), _authServerPort);

        spdlog::info("[InternalTest] (Attempt {}/{}) Attempting connection to AuthServer at {}:{}...", 
                     state->attempt, state->maxRetries, _authServerHost, _authServerPort);

        socket->async_connect(endpoint, [this, socket, state, timer, doConnect](const std::error_code& ec) {
            if (!ec)
            {
                spdlog::info("[InternalTest] Successfully connected to AuthServer at {}:{}", _authServerHost, _authServerPort);
                std::error_code closeEc;
                socket->close(closeEc);
            }
            else
            {
                spdlog::warn("[InternalTest] (Attempt {}/{}) Connection failed: {}", 
                             state->attempt, state->maxRetries, ec.message());
                
                if (state->attempt < state->maxRetries)
                {
                    state->attempt++;
                    timer->expires_after(state->delay);
                    timer->async_wait([doConnect](const std::error_code& timerEc) {
                        if (!timerEc)
                        {
                            (*doConnect)();
                        }
                    });
                }
                else
                {
                    spdlog::error("[InternalTest] Connection validation failed after {} attempts.", state->maxRetries);
                }
            }
        });
    };

    // 최초 호출 실행
    (*doConnect)();
}