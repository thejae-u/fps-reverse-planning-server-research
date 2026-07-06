#include "App.hpp"
#include "IOManager.hpp"
#include <cstdlib>
#include <ctime>

App::App() {}

App::~App()
{
    Shutdown();
}

bool App::Init()
{
    if(!glfwInit())
        return false;

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    _window = glfwCreateWindow(1280, 720, "FPS Test Client", nullptr, nullptr);
    if(!_window)
    {
        glfwTerminate();
        return false;
    }

    glfwMakeContextCurrent(_window);
    glfwSwapInterval(1);

    if(!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress))
    {
        return false;
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    (void)io;
    ImGui::StyleColorsDark();

    ImGui_ImplGlfw_InitForOpenGL(_window, true);
    ImGui_ImplOpenGL3_Init("#version 330");

    _ioManager = IOManager::Create("ClientIO", 4, 4);
    std::srand(static_cast<unsigned int>(std::time(nullptr)));
    return true;
}

void App::Run()
{
    while(!glfwWindowShouldClose(_window))
    {
        glfwPollEvents();

        UpdateAutoSend();

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        RenderUI();

        ImGui::Render();
        int display_w, display_h;
        glfwGetFramebufferSize(_window, &display_w, &display_h);
        glViewport(0, 0, display_w, display_h);
        glClearColor(0.45f, 0.55f, 0.60f, 1.00f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        glfwSwapBuffers(_window);
    }
}

void App::RenderUI()
{
    ImGui::Begin("Test Functions");

    ImGui::Text("TCP Functions");
    ImGui::InputText("TCP Msg", _messageToSend, IM_ARRAYSIZE(_messageToSend));
    ImGui::Separator();

    ImGui::Text("UDP Functions");
    ImGui::InputText("UDP Msg", _udpMessageToSend, IM_ARRAYSIZE(_udpMessageToSend));
    
    if (ImGui::TreeNode("Ingame Packets (Requires Room)"))
    {
        ImGui::Text("All Matched Clients:");
        if (ImGui::Button("Send Move##All")) SendIngamePacketsFromAll(IngameType::Move);
        ImGui::SameLine();
        if (ImGui::Button("Send Jump##All")) SendIngamePacketsFromAll(IngameType::Jump);
        ImGui::SameLine();
        if (ImGui::Button("Send Shoot##All")) SendIngamePacketsFromAll(IngameType::Shoot, (uint64_t)_targetTick);
        ImGui::SameLine();
        if (ImGui::Button("Send Hit##All")) SendIngamePacketsFromAll(IngameType::Hit);

        ImGui::InputInt("Target Tick (Lag Comp)", &_targetTick);

        ImGui::Separator();
        ImGui::Checkbox("Auto-send Move (Stress)", &_autoSendIngame);
        if (_autoSendIngame)
        {
            _autoSendRandom = false;
            ImGui::SliderFloat("Interval (sec)", &_autoSendInterval, 0.01f, 5.0f);
        }

        ImGui::Checkbox("Auto-send Random Packets (Shoot, Move, Jump)", &_autoSendRandom);
        if (_autoSendRandom)
        {
            _autoSendIngame = false;
            ImGui::SliderFloat("Interval (sec)##Random", &_autoSendInterval, 0.01f, 5.0f);
        }
        
        ImGui::TreePop();
    }

    ImGui::Separator();
    ImGui::Text("Stress Testing");
    ImGui::InputInt("Client Count", &_testClientCount);
    if(_testClientCount < 1)
        _testClientCount = 1;

    if(ImGui::Button("Connect Clients (Matchmaking Test)"))
    {
        StartMatchmakingTest(_testClientCount);
    }

    if(ImGui::Button("Disconnect All Clients (Matchmaking Test)"))
    {
        StopMatchmakingTest();
    }

    ImGui::Text("Active Test Clients: %zu", [this](){ std::lock_guard<std::mutex> lock(_testClientsMutex); return _testClients.size(); }());
    int connectedCount = 0;
    {
        std::lock_guard<std::mutex> lock(_testClientsMutex);
        for(const auto& client : _testClients)
        {
            if(client->IsConnected())
                connectedCount++;
        }
    }
    ImGui::Text("Connected Test Clients (TCP): %d", connectedCount);

    ImGui::End();

    ImGui::Begin("Client List");
    if (ImGui::BeginTable("ClientsTable", 6, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable))
    {
        ImGui::TableSetupColumn("Client");
        ImGui::TableSetupColumn("Status");
        ImGui::TableSetupColumn("Match Status");
        ImGui::TableSetupColumn("Room ID");
        ImGui::TableSetupColumn("Session ID");
        ImGui::TableSetupColumn("UDP Port");
        ImGui::TableHeadersRow();

        // Main Client
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
       // Test Clients
        {
            std::lock_guard<std::mutex> lock(_testClientsMutex);
            for (size_t i = 0; i < _testClients.size(); ++i)
            {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::Text("Test Client [%zu]", i);
                
                ImGui::TableSetColumnIndex(1);
                ImGui::TextColored(_testClients[i]->IsConnected() ? ImVec4(0, 1, 0, 1) : ImVec4(1, 0, 0, 1),
                                _testClients[i]->IsConnected() ? "Connected" : "Disconnected");

                ImGui::TableSetColumnIndex(2);
                if (_testClients[i]->IsConnected()) {
                    if (_testClients[i]->IsIngame()) ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "Ingame");
                    else if (!_testClients[i]->GetRoomId().empty()) ImGui::Text("Matched");
                    else if (_testClients[i]->IsMatching()) ImGui::Text("Matching...");
                    else ImGui::Text("Wait...");
                } else { ImGui::Text("-"); }

                ImGui::TableSetColumnIndex(3);
                ImGui::Text("%s", _testClients[i]->GetRoomId().empty() ? "-" : _testClients[i]->GetRoomId().substr(0, 8).c_str()); // 너무 길면 생략

                ImGui::TableSetColumnIndex(4);
                ImGui::Text("%s", _testClients[i]->GetSessionId().empty() ? "-" : _testClients[i]->GetSessionId().substr(0, 8).c_str());

                ImGui::TableSetColumnIndex(5);
                ImGui::Text("%u", _testClients[i]->GetClientUdpPort());
            }
        }
        ImGui::EndTable();
    }
    ImGui::End();

    ImGui::Begin("Received Messages");
    {
        std::lock_guard<std::mutex> lock(_messagesMutex);
        for(const auto& msg : _receivedMessages)
        {
            ImGui::Text("%s", msg.c_str());
        }
        if(ImGui::GetScrollY() >= ImGui::GetScrollMaxY())
            ImGui::SetScrollHereY(1.0f);
    }
    ImGui::End();
}

void App::StartMatchmakingTest(int count)
{
    StopMatchmakingTest();

    std::thread([this, count]() {
        for(int i = 0; i < count; ++i)
        {
            auto client = std::make_shared<NetworkClient>(_ioManager);
            client->SetMessageCallback([this, i](const std::string& msg) {
                OnMessage("Test Client [" + std::to_string(i) + "] TCP: " + msg);
            });
            
            client->Connect(_host, (uint16_t)_port);
            
            {
                std::lock_guard<std::mutex> lock(_testClientsMutex);
                _testClients.push_back(client);
            }
            
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        spdlog::info("Started matchmaking test with {} clients", count);
    }).detach();
}

void App::StopMatchmakingTest()
{
    std::lock_guard<std::mutex> lock(_testClientsMutex);
    if(_testClients.empty())
        return;

    for(auto& client : _testClients)
    {
        client->Disconnect();
    }
    _testClients.clear();
    spdlog::info("Stopped all tests");
}

void App::OnMessage(const std::string& message)
{
    std::lock_guard<std::mutex> lock(_messagesMutex);
    _receivedMessages.push_back(message);
    if(_receivedMessages.size() > 100)
    {
        _receivedMessages.erase(_receivedMessages.begin());
    }
}

void App::Shutdown()
{
    StopMatchmakingTest();

    if (_ioManager)
    {
        _ioManager->Stop();
    }

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    if(_window)
    {
        glfwDestroyWindow(_window);
    }
    glfwTerminate();
}

void App::SendIngamePacketsFromAll(IngameType type, uint64_t clientTick)
{
    std::lock_guard<std::mutex> lock(_testClientsMutex);
    for (auto& client : _testClients)
    {
        if (client->IsConnected() && !client->GetRoomId().empty())
        {
            if (type == IngameType::Move)
            {
                struct MoveData {
                    float dx = 1.0f;
                    float dy = 0.0f;
                    float dz = 0.0f;
                    std::int32_t speed = 10;
                } data;
                std::string sendData(sizeof(MoveData), '\0');
                std::memcpy(&sendData[0], &data, sizeof(MoveData));
                client->SendIngamePacket(type, sendData, clientTick);
            }
            else if (type == IngameType::Shoot)
            {
                struct ShootData {
                    float x = 1.0f;
                    float y = 0.0f;
                    float z = 0.0f;
                } data;
                std::string sendData(sizeof(ShootData), '\0');
                std::memcpy(&sendData[0], &data, sizeof(ShootData));
                client->SendIngamePacket(type, sendData, clientTick);
            }
            else
            {
                client->SendIngamePacket(type, "TestClientPacket", clientTick);
            }
        }
    }
}

void App::UpdateAutoSend()
{
    if (!_autoSendIngame && !_autoSendRandom)
        return;

    double currentTime = glfwGetTime();
    if (currentTime - _lastAutoSendTime >= _autoSendInterval)
    {
        if (_autoSendIngame)
        {
            SendIngamePacketsFromAll(IngameType::Move);
        }
        else if (_autoSendRandom)
        {
            std::lock_guard<std::mutex> lock(_testClientsMutex);
            for (auto& client : _testClients)
            {
                if (client->IsConnected() && client->IsIngame())
                {
                    int packetType = rand() % 3; // 0: Move, 1: Jump, 2: Shoot
                    if (packetType == 0)
                    {
                        struct MoveData {
                            float dx = 0.0f;
                            float dy = 0.0f;
                            float dz = 0.0f;
                            std::int32_t speed = 10;
                        } data;
                        data.dx = static_cast<float>(rand()) / static_cast<float>(RAND_MAX) * 2.0f - 1.0f;
                        data.dz = static_cast<float>(rand()) / static_cast<float>(RAND_MAX) * 2.0f - 1.0f;
                        data.speed = rand() % 15 + 5; // 5 to 20

                        std::string sendData(sizeof(MoveData), '\0');
                        std::memcpy(&sendData[0], &data, sizeof(MoveData));
                        client->SendIngamePacket(IngameType::Move, sendData);
                    }
                    else if (packetType == 1)
                    {
                        client->SendIngamePacket(IngameType::Jump, "");
                    }
                    else if (packetType == 2)
                    {
                        struct ShootData {
                            float x = 0.0f;
                            float y = 0.0f;
                            float z = 1.0f;
                        } data;
                        data.x = static_cast<float>(rand()) / static_cast<float>(RAND_MAX) * 2.0f - 1.0f;
                        data.z = static_cast<float>(rand()) / static_cast<float>(RAND_MAX) * 2.0f - 1.0f;

                        std::string sendData(sizeof(ShootData), '\0');
                        std::memcpy(&sendData[0], &data, sizeof(ShootData));
                        client->SendIngamePacket(IngameType::Shoot, sendData, (uint64_t)_targetTick);
                    }
                }
            }
        }
        _lastAutoSendTime = currentTime;
    }
}
