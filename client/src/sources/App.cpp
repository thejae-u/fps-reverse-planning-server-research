#include "App.hpp"

App::App() {}

App::~App() {
    Shutdown();
}

bool App::Init() {
    if (!glfwInit()) return false;

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    _window = glfwCreateWindow(1280, 720, "FPS Test Client", nullptr, nullptr);
    if (!_window) {
        glfwTerminate();
        return false;
    }

    glfwMakeContextCurrent(_window);
    glfwSwapInterval(1);

    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
        return false;
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    ImGui::StyleColorsDark();

    ImGui_ImplGlfw_InitForOpenGL(_window, true);
    ImGui_ImplOpenGL3_Init("#version 330");

    _networkClient.SetMessageCallback([this](const std::string& msg) {
        OnMessage(msg);
    });

    return true;
}

void App::Run() {
    while (!glfwWindowShouldClose(_window)) {
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

void App::RenderUI() {
    ImGui::Begin("Connection Control");
    ImGui::InputText("Host", _host, IM_ARRAYSIZE(_host));
    ImGui::InputInt("Port", &_port);

    if (!_networkClient.IsConnected()) {
        if (ImGui::Button("Connect")) {
            _networkClient.Connect(_host, (uint16_t)_port);
        }
    } else {
        if (ImGui::Button("Disconnect")) {
            _networkClient.Disconnect();
        }
    }

    ImGui::Separator();
    ImGui::Text("Status: %s", _networkClient.IsConnected() ? "Connected" : "Disconnected");
    ImGui::End();

    ImGui::Begin("Test Functions");
    if (ImGui::Button("Send Match Request")) {
        _networkClient.Send("111");
    }

    ImGui::Separator();
    ImGui::Text("Matchmaking Stress Test");
    ImGui::InputInt("Client Count", &_testClientCount);
    if (_testClientCount < 1) _testClientCount = 1;

    if (ImGui::Button("Start Test")) {
        StartMatchmakingTest(_testClientCount);
    }
    ImGui::SameLine();
    if (ImGui::Button("Stop Test")) {
        StopMatchmakingTest();
    }

    ImGui::Text("Active Test Clients: %zu", _testClients.size());
    int connectedCount = 0;
    for (const auto& client : _testClients) {
        if (client->IsConnected()) connectedCount++;
    }
    ImGui::Text("Connected Test Clients: %d", connectedCount);

    ImGui::End();

    ImGui::Begin("Received Messages");
    {
        std::lock_guard<std::mutex> lock(_messagesMutex);
        for (const auto& msg : _receivedMessages) {
            ImGui::Text("%s", msg.c_str());
        }
    }
    if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY())
        ImGui::SetScrollHereY(1.0f);
    ImGui::End();

    ImGui::Begin("Logs");
    for (const auto& log : _networkClient.GetLogs()) {
        ImVec4 color = ImVec4(1, 1, 1, 1);
        if (log.level == spdlog::level::err) color = ImVec4(1, 0, 0, 1);
        else if (log.level == spdlog::level::warn) color = ImVec4(1, 1, 0, 1);
        
        ImGui::TextColored(color, "%s", log.text.c_str());
    }
    if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY())
        ImGui::SetScrollHereY(1.0f);
    ImGui::End();
}

void App::StartMatchmakingTest(int count) {
    StopMatchmakingTest();

    for (int i = 0; i < count; ++i) {
        auto client = std::make_unique<NetworkClient>();
        client->SetMessageCallback([this, i](const std::string& msg) {
            OnMessage("Test Client [" + std::to_string(i) + "]: " + msg);
        });
        client->Connect(_host, (uint16_t)_port);
        _testClients.push_back(std::move(client));
    }
    spdlog::info("Started matchmaking test with {} clients", count);
}

void App::StopMatchmakingTest() {
    if (_testClients.empty()) return;

    for (auto& client : _testClients) {
        client->Disconnect();
    }
    _testClients.clear();
    spdlog::info("Stopped matchmaking test");
}

void App::OnMessage(const std::string& message) {
    std::lock_guard<std::mutex> lock(_messagesMutex);
    _receivedMessages.push_back(message);
}

void App::Shutdown() {
    StopMatchmakingTest();

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    if (_window) {
        glfwDestroyWindow(_window);
    }
    glfwTerminate();
}
