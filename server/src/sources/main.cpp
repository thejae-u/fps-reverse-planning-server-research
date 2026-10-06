#include "Base.hpp"
#include "ServerBuildInfo.hpp"
#include <nlohmann/json.hpp>
#include "ServerPolicy.hpp"
#include "ExecutionContext.hpp"
#include "Listener.hpp"
#include "PacketPool.hpp"
#include "Room.hpp"
#include "BlockingExecutor.hpp"

int main(const int argc, char **argv)
{
    // 네트워크를 시작하지 않고 실행 파일에 포함된 빌드 정보를 반환
    if (argc == 2 && std::string_view(argv[1]) == "--build-info")
    {
#ifdef NDEBUG
        constexpr bool assertionsEnabled = false;
#else
        constexpr bool assertionsEnabled = true;
#endif
        const nlohmann::json info = {
            {"configuration", ServerBuildInfo::Configuration},
            {"benchmark_protocol_version", 2},
            {"tick_interval_us", ServerPolicy::TickInterval.count()},
            {"connection_input_rate_limit", ServerPolicy::ConnectionInputRate},
            {"global_udp_rate_limit", ServerPolicy::GlobalUdpRate},
            {"compiler", ServerBuildInfo::Compiler},
            {"compiler_version", ServerBuildInfo::CompilerVersion},
            {"compiler_path", ServerBuildInfo::CompilerPath},
            {"target_os", ServerBuildInfo::System},
            {"target_arch", ServerBuildInfo::Processor},
            {"generator", ServerBuildInfo::Generator},
            {"cmake_version", ServerBuildInfo::CmakeVersion},
            {"cmake_cxx_flags", ServerBuildInfo::Flags},
            {"cxx_standard", 20},
            {"assertions_enabled", assertionsEnabled},
            {"network_workers", std::thread::hardware_concurrency()},
            {"game_workers", ServerPolicy::GameWorkers},
            {"blocking_workers", ServerPolicy::BlockingWorkers}
        };
        std::cout << info.dump() << '\n';
        return 0;
    }

    // 실행인자 파싱 및 검증
    if (argc == 1)
    {
        spdlog::error("no options");
        exit(0);
    }

    const ServerConfig config = ServerConfig::Parse(argc, argv);
    if (config.matchId.empty())
    {
        spdlog::error("invalid match id");
        exit(0);
    }

    if (config.authToken.empty())
    {
        spdlog::error("invalid auth token");
        exit(0);
    }

    if (config.tcpPort == 0 || config.udpPort == 0)
    {
        spdlog::error("invalid port");
        exit(0);
    }

    if (config.allowedPlayers.empty() || config.allowedPlayers.size() > ServerPolicy::MaximumPlayers)
    {
        spdlog::error("player count must be between 1 and {}", ServerPolicy::MaximumPlayers);
        exit(0);
    }

    // 서버 시작
    spdlog::info("type 'quit' to stop server");
    const auto threadCount = std::thread::hardware_concurrency();
    constexpr auto blockingThreadCount = ServerPolicy::BlockingWorkers;
    const auto ioManager = ExecutionContext::Create("Network", threadCount);
    const auto gameManager = ExecutionContext::Create("Game Simulation", ServerPolicy::GameWorkers);
    const auto blocking = std::make_shared<BlockingExecutor>(blockingThreadCount);

    if (!ioManager)
        throw std::runtime_error("failed to create Network execution context");

    constexpr auto ingamePacketPoolSize = ServerPolicy::PoolCapacity;
    constexpr auto networkPacketPoolSize = ServerPolicy::PoolCapacity;
    constexpr auto byteBufferPoolSize = ServerPolicy::PoolCapacity;

    IngamePacketPool::Init(ingamePacketPoolSize);
    NetworkPacketPool::Init(networkPacketPoolSize);
    ByteBufferPool::Init(byteBufferPoolSize);

    // Main Thread와 detach된 consoleThread 간에 안전하게 수명을 공유하는 종료 동기화 상태
    struct ShutdownState
    {
        std::mutex mutex;
        std::condition_variable cv;
        std::atomic<bool> isShuttingDown{false};

        void RequestShutdown()
        {
            if (!isShuttingDown.exchange(true))
            {
                std::lock_guard<std::mutex> lock(mutex);
                cv.notify_all();
            }
        }
    };

    const auto shutdownState = std::make_shared<ShutdownState>();

    // broadcast용 room
    auto matchId = uuids::uuid::from_string(config.matchId).value_or(uuids::uuid_system_generator{}());
    const auto dedicatedRoom =
        Room::Create(gameManager, blocking, matchId, config.authToken, config.allowedPlayers.size());
    dedicatedRoom->SetShutdownCallback([shutdownState]() {
        shutdownState->RequestShutdown();
    });

    const auto listener = Listener::Create(ioManager, config.tcpPort, config.udpPort, config.allowedPlayers);
    listener->SetDedicatedRoom(dedicatedRoom);
    dedicatedRoom->SetGateway(listener);

    // Start, Stop을 처리하기 위한 컨테이너
    std::vector<std::shared_ptr<IBase>> components;
    components.emplace_back(listener);

    for (const auto &component : components)
        component->Start();

    // 콘솔 입력 스레드는 shared_ptr<ShutdownState>만 값으로 캡처하여
    // Main Thread 종료 후에도 스택 댕글링 참조가 발생하지 않도록 보장
    std::thread consoleThread([shutdownState]() {
        std::string tmp;
        while (std::cin >> tmp)
        {
            spdlog::info("[Main] Received console input: '{}'", tmp);
            if (tmp == "quit")
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
        shutdownState->cv.wait(lock, [&]() {
            return shutdownState->isShuttingDown.load();
        });
    }

    spdlog::info("[Main] Shutting down server components on Main Thread...");
    listener->StopInput();
    dedicatedRoom->Stop();
    gameManager->Drain();
    blocking->Join();

    // Game has submitted its last messages. Network remains alive during bounded drain.
    const auto deadline = std::chrono::steady_clock::now() + ServerPolicy::NetworkDrainTimeout;
    while (!listener->IsDrained() && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(ServerPolicy::DrainPollInterval);

    if (!listener->IsDrained())
        spdlog::warn("Network drain deadline reached; canceling remaining socket operations");

    listener->Stop();
    listener->SetDedicatedRoom(nullptr);
    ioManager->Drain();

    IngamePacketPool::Release();
    NetworkPacketPool::Release();
    ByteBufferPool::Release();

    spdlog::info("[Main] Server shutdown complete.");
    return 0;
}