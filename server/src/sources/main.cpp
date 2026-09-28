#include "Base.hpp"
#include "IOManager.hpp"
#include "Listener.hpp"
#include "PacketPool.hpp"
#include "Room.hpp"

int main(const int argc, char** argv)
{
    // 실행인자 파싱 및 검증
    if(argc == 1)
    {
        spdlog::error("no options");
        exit(0);
    }

    const ServerConfig config = ServerConfig::Parse(argc, argv);
    if(config.matchId.empty())
    {
        spdlog::error("invalid match id");
        exit(0);
    }
    
    if(config.authToken.empty())
    {
        spdlog::error("invalid auth token");
        exit(0);
    }
    
    if(config.tcpPort == 0 || config.udpPort == 0)
    {
        spdlog::error("invalid port");
        exit(0);
    }
    
    if(config.allowedPlayers.size() == 0)
    {
        spdlog::error("no players specified");
        exit(0);
    }
    
    // 서버 시작
    spdlog::info("type 'quit' to stop server");
    const auto threadCount = std::thread::hardware_concurrency();
    constexpr auto blockingThreadCount = 4;
    const auto ioManager = IOManager::Create("I/O Manager", threadCount, blockingThreadCount);
    
    if(!ioManager)
        throw std::runtime_error("failed to create io manager");
    
    constexpr auto ingamePacketPoolSize = 500;
    constexpr auto networkPacketPoolSize = 500;
    constexpr auto byteBufferPoolSize = 500;

    IngamePacketPool::Init(ingamePacketPoolSize);
    NetworkPacketPool::Init(networkPacketPoolSize);
    ByteBufferPool::Init(byteBufferPoolSize);

    // Main Thread와 detach된 consoleThread 간에 안전하게 수명을 공유하는 종료 동기화 상태
    struct ShutdownState
    {
        std::mutex mutex;
        std::condition_variable cv;
        std::atomic<bool> isShuttingDown{ false };

        void RequestShutdown()
        {
            if(!isShuttingDown.exchange(true))
            {
                std::lock_guard<std::mutex> lock(mutex);
                cv.notify_all();
            }
        }
    };
    const auto shutdownState = std::make_shared<ShutdownState>();

    // broadcast용 room
    auto matchId = uuids::uuid::from_string(config.matchId).value_or(uuids::uuid_system_generator{}());
    const auto dedicatedRoom = Room::Create(ioManager, matchId, config.authToken, config.allowedPlayers.size());
    dedicatedRoom->SetShutdownCallback([shutdownState]() {
        shutdownState->RequestShutdown();
    });

    const auto listener = Listener::Create(ioManager, config.tcpPort, config.udpPort, config.allowedPlayers);
    listener->SetDedicatedRoom(dedicatedRoom);

    // Start, Stop을 처리하기 위한 컨테이너
    std::vector<std::shared_ptr<IBase>> components;
    components.emplace_back(listener);

    for(const auto& component : components)
        component->Start();

    // 콘솔 입력 스레드는 shared_ptr<ShutdownState>만 값으로 캡처하여
    // Main Thread 종료 후에도 스택 댕글링 참조가 발생하지 않도록 보장
    std::thread consoleThread([shutdownState]() {
        std::string tmp;
        while(std::cin >> tmp)
        {
            spdlog::info("[Main] Received console input: '{}'", tmp);
            if(tmp == "quit")
            {
                shutdownState->RequestShutdown();
                break;
            }
        }
    });
    consoleThread.detach();

    // Main Thread: 매치 종료(OnMatchFinished) 또는 콘솔 종료(quit) 신호 대기
    {
        std::unique_lock<std::mutex> lock(shutdownState->mutex);
        shutdownState->cv.wait(lock, [&]() { return shutdownState->isShuttingDown.load(); });
    }

    spdlog::info("[Main] Shutting down server components on Main Thread...");
    for(auto it = components.rbegin(); it != components.rend(); ++it)
        (*it)->Stop();

    ioManager->Stop();
    
    IngamePacketPool::Release();
    NetworkPacketPool::Release();
    ByteBufferPool::Release();

    spdlog::info("[Main] Server shutdown complete.");
    return 0;
}
