#include "App.hpp"
#include "IOManager.hpp"

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
        if (ImGui::Button("Send Shoot##All")) SendIngamePacketsFromAll(IngameType::Shoot);
        ImGui::SameLine();
        if (ImGui::Button("Send Hit##All")) SendIngamePacketsFromAll(IngameType::Hit);

        ImGui::Separator();
        ImGui::Checkbox("Auto-send Move (Stress)", &_autoSendIngame);
        if (_autoSendIngame)
        {
            ImGui::SliderFloat("Interval (sec)", &_autoSendInterval, 0.01f, 5.0f);
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
                    if (!_testClients[i]->GetRoomId().empty()) ImGui::Text("Matched");
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

    std::lock_guard<std::mutex> lock(_testClientsMutex);
    for(int i = 0; i < count; ++i)
    {
        auto client = std::make_shared<NetworkClient>(_ioManager);
        client->SetMessageCallback([this, i](const std::string& msg) {
            OnMessage("Test Client [" + std::to_string(i) + "] TCP: " + msg);
        });
        
        client->Connect(_host, (uint16_t)_port);
        
        // Matchmaking request is now handled automatically after handshake success
        // or can be triggered immediately if connection is already established
        _ioManager->RegisterAsyncWork([client]() {
            if (client->IsConnected()) {
                client->SendMatchRequest();
            }
        });

        _testClients.push_back(client);
    }

    spdlog::info("Started matchmaking test with {} clients", count);
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

void App::SendIngamePacketsFromAll(IngameType type)
{
    std::lock_guard<std::mutex> lock(_testClientsMutex);
    for (auto& client : _testClients)
    {
        if (client->IsConnected() && !client->GetRoomId().empty())
        {
            client->SendIngamePacket(type, "TestClientPacket");
        }
    }
}

void App::UpdateAutoSend()
{
    if (!_autoSendIngame)
        return;

    double currentTime = glfwGetTime();
    if (currentTime - _lastAutoSendTime >= _autoSendInterval)
    {
        SendIngamePacketsFromAll(IngameType::Move);
        _lastAutoSendTime = currentTime;
    }
}
