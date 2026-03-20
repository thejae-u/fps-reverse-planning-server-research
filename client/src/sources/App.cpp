#include "App.hpp"

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

    _networkClient.SetMessageCallback([this](const std::string& msg) {
        OnMessage("[TCP]: " + msg);
    });

    _networkClient.SetUdpMessageCallback([this](const std::string& msg) {
        OnMessage("[UDP]: " + msg);
    });

    return true;
}

void App::Run()
{
    while(!glfwWindowShouldClose(_window))
    {
        glfwPollEvents();

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
    ImGui::Begin("Connection Control");
    ImGui::InputText("Host", _host, IM_ARRAYSIZE(_host));
    ImGui::InputInt("TCP Port", &_port);
    ImGui::InputInt("UDP Port", &_udpPort);

    if(!_networkClient.IsConnected())
    {
        if(ImGui::Button("Connect (TCP)"))
        {
            _networkClient.Connect(_host, (uint16_t)_port);
        }
    }
    else
    {
        if(ImGui::Button("Disconnect (TCP)"))
        {
            _networkClient.Disconnect();
        }
    }

    ImGui::Separator();
    ImGui::Text("TCP Status: %s", _networkClient.IsConnected() ? "Connected" : "Disconnected");
    ImGui::End();

    ImGui::Begin("Test Functions");

    ImGui::Text("TCP Functions");
    ImGui::InputText("TCP Msg", _messageToSend, IM_ARRAYSIZE(_messageToSend));
    if(ImGui::Button("Send TCP Message"))
    {
        _networkClient.Send(_messageToSend);
    }
    if(ImGui::Button("Send Match Request (111)"))
    {
        _networkClient.SetMatching(true);
        _networkClient.Send("111");
    }

    ImGui::Separator();

    ImGui::Text("UDP Functions");
    ImGui::InputText("UDP Msg", _udpMessageToSend, IM_ARRAYSIZE(_udpMessageToSend));
    if(ImGui::Button("Send UDP (Correct Protocol)"))
    {
        _networkClient.SendUdpCorrect(_udpMessageToSend, _host, (uint16_t)_udpPort);
    }

    ImGui::PushStyleColor(ImGuiCol_Button, (ImVec4)ImColor::HSV(0.0f, 0.6f, 0.6f));
    if(ImGui::Button("Send UDP (Size < 2 bytes)"))
    {
        _networkClient.SendUdpMalformed(_udpMessageToSend, _host, (uint16_t)_udpPort, 1);
    }
    if(ImGui::Button("Send UDP (Mismatched Header)"))
    {
        _networkClient.SendUdpMalformed(_udpMessageToSend, _host, (uint16_t)_udpPort, 2);
    }
    ImGui::PopStyleColor();

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

    ImGui::Text("Active Test Clients: %zu", _testClients.size());
    int connectedCount = 0;
    for(const auto& client : _testClients)
    {
        if(client->IsConnected())
            connectedCount++;
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
        ImGui::Text("Main Client");

        ImGui::TableSetColumnIndex(1);
        ImGui::TextColored(_networkClient.IsConnected() ? ImVec4(0, 1, 0, 1) : ImVec4(1, 0, 0, 1),
                           _networkClient.IsConnected() ? "Connected" : "Disconnected");

        ImGui::TableSetColumnIndex(2);
        if (_networkClient.IsConnected()) {
            if (_networkClient.IsMatching())
                ImGui::TextColored(ImVec4(1, 1, 0, 1), "매칭 중");
            else if (!_networkClient.GetRoomId().empty())
                ImGui::TextColored(ImVec4(0, 1, 1, 1), "매칭 완료");
            else
                ImGui::Text("대기");
        } else {
            ImGui::Text("-");
        }

        ImGui::TableSetColumnIndex(3);
        ImGui::Text("%s", _networkClient.GetRoomId().empty() ? "-" : _networkClient.GetRoomId().c_str());

        ImGui::TableSetColumnIndex(4);
        ImGui::Text("%s", _networkClient.GetSessionId().empty() ? "-" : _networkClient.GetSessionId().c_str());

        ImGui::TableSetColumnIndex(5);
        ImGui::Text("%u", _networkClient.GetClientUdpPort());

        // Test Clients
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
                if (_testClients[i]->IsMatching()) ImGui::Text("Matching...");
                else if (!_testClients[i]->GetRoomId().empty()) ImGui::Text("Matched");
                else ImGui::Text("Wait...");
            } else { ImGui::Text("-"); }

            ImGui::TableSetColumnIndex(3);
            ImGui::Text("%s", _testClients[i]->GetRoomId().empty() ? "-" : _testClients[i]->GetRoomId().substr(0, 8).c_str()); // 너무 길면 생략

            ImGui::TableSetColumnIndex(4);
            ImGui::Text("%s", _testClients[i]->GetSessionId().empty() ? "-" : _testClients[i]->GetSessionId().substr(0, 8).c_str());

            ImGui::TableSetColumnIndex(5);
            ImGui::Text("%u", _testClients[i]->GetClientUdpPort());
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

    ImGui::Begin("Logs");
    for(const auto& log : _networkClient.GetLogs())
    {
        ImVec4 color = ImVec4(1, 1, 1, 1);
        if(log.level == spdlog::level::err)
            color = ImVec4(1, 0, 0, 1);
        else if(log.level == spdlog::level::warn)
            color = ImVec4(1, 1, 0, 1);

        ImGui::TextColored(color, "%s", log.text.c_str());
    }
    if(ImGui::GetScrollY() >= ImGui::GetScrollMaxY())
        ImGui::SetScrollHereY(1.0f);
    ImGui::End();
}

void App::StartMatchmakingTest(int count)
{
    StopMatchmakingTest();

    for(int i = 0; i < count; ++i)
    {
        auto client = std::make_unique<NetworkClient>();
        client->SetMessageCallback([this, i](const std::string& msg) {
            OnMessage("Test Client [" + std::to_string(i) + "] TCP: " + msg);
        });
        client->Connect(_host, (uint16_t)_port);
        _testClients.push_back(std::move(client));
    }
    spdlog::info("Started matchmaking test with {} clients", count);
}

void App::StopMatchmakingTest()
{
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

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    if(_window)
    {
        glfwDestroyWindow(_window);
    }
    glfwTerminate();
}
