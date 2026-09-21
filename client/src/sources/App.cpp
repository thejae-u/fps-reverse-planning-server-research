#include "App.hpp"
#include <cpr/cpr.h>
#include <chrono>
#include <thread>
#include <format>

App::App()
{
}

App::~App()
{
    Shutdown();
}

bool App::Init()
{
    if (!glfwInit())
    {
        spdlog::error("Failed to initialize GLFW");
        return false;
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    _window = glfwCreateWindow(1280, 800, "FPS Client - Auth & Dedicated Server Controller", nullptr, nullptr);
    if (!_window)
    {
        spdlog::error("Failed to create GLFW window");
        glfwTerminate();
        return false;
    }

    glfwMakeContextCurrent(_window);
    glfwSwapInterval(1);

    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress))
    {
        spdlog::error("Failed to initialize GLAD");
        return false;
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    (void)io;
    ImGui::StyleColorsDark();

    ImGui_ImplGlfw_InitForOpenGL(_window, true);
    ImGui_ImplOpenGL3_Init("#version 330");

    _ioManager = IOManager::Create("ClientIO", 2, 2);
    _dedicatedClient = std::make_shared<DedicatedClient>(_ioManager);

    _dedicatedClient->SetOnIngameReady([this]() {
        AddLog("[Event] Ingame Ready confirmed by Dedicated Server!");
        if (_autoSendRandomInput)
        {
            _dedicatedClient->StartRandomInput(_randomInputIntervalMs);
        }
    });

    _dedicatedClient->SetOnDisconnected([this](const std::string& reason) {
        AddLog("[Event] Disconnected from server: " + reason);
        if (!_isAutomatedTesting)
        {
            std::thread([this]() {
                std::this_thread::sleep_for(std::chrono::milliseconds(500));
                HttpCheckStatus();
                ShowFinishBanner("MATCH TERMINATED", std::format("Session ended. Packets sent: {}", _dedicatedClient ? _dedicatedClient->GetRandomPacketsSent() : 0));
            }).detach();
        }
    });

    AddLog("[System] Client initialized successfully.");
    return true;
}

void App::Run()
{
    while (!glfwWindowShouldClose(_window))
    {
        glfwPollEvents();

        // Consume dedicated client logs
        if (_dedicatedClient)
        {
            auto clientLogs = _dedicatedClient->ConsumeLogs();
            for (const auto& log : clientLogs)
            {
                AddLog(log);
            }
        }

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        RenderUI();

        ImGui::Render();
        int display_w, display_h;
        glfwGetFramebufferSize(_window, &display_w, &display_h);
        glViewport(0, 0, display_w, display_h);
        glClearColor(0.12f, 0.14f, 0.18f, 1.00f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        glfwSwapBuffers(_window);
    }
}

void App::Shutdown()
{
    if (_dedicatedClient)
    {
        _dedicatedClient->Disconnect();
        _dedicatedClient = nullptr;
    }

    if (_ioManager)
    {
        _ioManager->Stop();
        _ioManager = nullptr;
    }

    if (_window)
    {
        ImGui_ImplOpenGL3_Shutdown();
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();

        glfwDestroyWindow(_window);
        _window = nullptr;
        glfwTerminate();
    }
}

void App::AddLog(const std::string& msg)
{
    std::lock_guard<std::mutex> lock(_logsMutex);
    _logs.push_back(msg);
}

void App::RenderUI()
{
    // Fullscreen dockspace style
    ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(1260, 780), ImGuiCond_Always);

    ImGui::Begin("FPS Client Control Center", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse);

    ImGui::Columns(2, "MainColumns", true);
    ImGui::SetColumnWidth(0, 580.0f);

    // Left Column: Auth Server & Matchmaking
    RenderAuthPanel();

    ImGui::NextColumn();

    // Right Column: Dedicated Server Connection & Ingame controls
    RenderDedicatedPanel();
    ImGui::Separator();
    RenderLogPanel();

    ImGui::Columns(1);
    ImGui::End();

    RenderFinishModal();
}

void App::RenderAuthPanel()
{
    ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "1. AuthServer & Matchmaking Management");
    ImGui::Separator();

    ImGui::InputText("Auth Server URL", _authServerUrl, sizeof(_authServerUrl));
    ImGui::InputText("Username", _username, sizeof(_username));
    ImGui::InputText("Password", _password, sizeof(_password), ImGuiInputTextFlags_Password);

    if (ImGui::Button("Register"))
    {
        HttpRegister();
    }
    ImGui::SameLine();
    if (ImGui::Button("Login"))
    {
        HttpLogin();
    }

    ImGui::Spacing();
    if (!_jwtToken.empty())
    {
        ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.3f, 1.0f), "Logged In as: %s", _username);
        ImGui::Text("User ID: %s", _userId.c_str());
        std::string tokenPreview = _jwtToken.substr(0, std::min<size_t>(30, _jwtToken.size())) + "...";
        ImGui::Text("JWT: %s", tokenPreview.c_str());
    }
    else
    {
        ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "Not Logged In");
    }

    ImGui::Separator();
    ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.3f, 1.0f), "Matchmaking Queue Controls");

    if (ImGui::Button("Join Queue"))
    {
        HttpJoinQueue();
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel Queue"))
    {
        HttpCancelQueue();
    }
    ImGui::SameLine();
    if (ImGui::Button("Check Status"))
    {
        HttpCheckStatus();
    }

    ImGui::Spacing();
    ImGui::Text("Match Status: ");
    ImGui::SameLine();
    if (_matchStatus == "Matched")
        ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.2f, 1.0f), "%s", _matchStatus.c_str());
    else if (_matchStatus == "Waiting")
        ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "%s", _matchStatus.c_str());
    else
        ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "%s", _matchStatus.c_str());

    if (!_matchId.empty())
        ImGui::Text("Match ID: %s", _matchId.c_str());
    if (!_serverAddress.empty())
        ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.8f, 1.0f), "Assigned Server: %s", _serverAddress.c_str());

    ImGui::Spacing();
    ImGui::Separator();

    // 10-player match trigger button
    ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.0f, 1.0f), "Instant 10-Player Match Testing:");
    ImGui::TextWrapped("Click below to auto-register & join 9 bot players alongside your current player, immediately triggering Dedicated Server process spawn.");

    if (_isTriggeringMatch)
    {
        ImGui::BeginDisabled();
        ImGui::Button("Matchmaking In Progress...", ImVec2(350, 40));
        ImGui::EndDisabled();
    }
    else
    {
        if (ImGui::Button(">> Trigger 10-Player Match & Spawn Server <<", ImVec2(350, 40)))
        {
            TriggerTenPlayerMatch();
        }
    }

    ImGui::Spacing();
    ImGui::Separator();

    // Automated E2E Lifecycle & Combat Test (Bat Migration)
    ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.5f, 1.0f), "Full E2E Lifecycle & Combat Test (Bat Migration):");
    ImGui::TextWrapped("Fully automated test: Queues 10 players, waits for Dedicated Server, connects socket & UDP hole punch, streams random inputs, and displays match summary!");

    if (_isAutomatedTesting)
    {
        ImGui::BeginDisabled();
        ImGui::Button("E2E Test Running in Progress...", ImVec2(380, 45));
        ImGui::EndDisabled();
    }
    else
    {
        if (ImGui::Button("▶ RUN AUTOMATED E2E TEST (MIGRATION)", ImVec2(380, 45)))
        {
            RunAutomatedLifecycleTest();
        }
    }
}

void App::RenderDedicatedPanel()
{
    ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "2. Dedicated Server Connection (TCP & UDP)");
    ImGui::Separator();

    ImGui::InputText("Target Host", _targetHost, sizeof(_targetHost));
    ImGui::InputInt("Target TCP Port", &_targetPort);

    bool isConnected = _dedicatedClient && _dedicatedClient->IsConnected();
    bool isIngame = _dedicatedClient && _dedicatedClient->IsIngame();

    if (!isConnected)
    {
        if (ImGui::Button("Connect to Dedicated Server", ImVec2(220, 30)))
        {
            if (_targetPort > 0)
                _dedicatedClient->Connect(_targetHost, static_cast<uint16_t>(_targetPort));
            else
                AddLog("[Client] Please specify a valid target port first.");
        }
    }
    else
    {
        if (ImGui::Button("Disconnect", ImVec2(120, 30)))
        {
            _dedicatedClient->Disconnect();
        }
    }

    ImGui::Spacing();
    ImGui::Text("Connection State: ");
    ImGui::SameLine();
    if (isIngame)
        ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.2f, 1.0f), "[INGAME READY] (UDP Hole Punched)");
    else if (isConnected)
        ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.2f, 1.0f), "[TCP CONNECTED] (Waiting for UDP Hole Punching)");
    else
        ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "[DISCONNECTED]");

    if (isConnected)
    {
        ImGui::Text("Assigned Session ID: %s", _dedicatedClient->GetSessionId().c_str());
        ImGui::Text("Server UDP Port: %d", _dedicatedClient->GetServerUdpPort());
        ImGui::Text("Local UDP Port: %d", _dedicatedClient->GetClientUdpPort());
    }

    ImGui::Spacing();
    ImGui::TextColored(ImVec4(1.0f, 0.9f, 0.5f, 1.0f), "Ingame Test Packets:");
    if (!isIngame)
        ImGui::BeginDisabled();

    if (ImGui::Button("Send Move"))
    {
        Protocol::MovePacket move;
        move.set_playerid(_dedicatedClient->GetSessionId());
        move.set_originx(10.0f);
        move.set_originy(0.0f);
        move.set_originz(10.0f);
        move.set_dirx(1.0f);
        move.set_diry(0.0f);
        move.set_dirz(0.0f);

        std::string serialized;
        if (move.SerializeToString(&serialized))
        {
            _dedicatedClient->SendIngamePacket(Protocol::IngameType::Move, serialized);
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Send Jump"))
    {
        _dedicatedClient->SendIngamePacket(Protocol::IngameType::Jump, "");
    }
    ImGui::SameLine();
    if (ImGui::Button("Send Shoot"))
    {
        _dedicatedClient->SendIngamePacket(Protocol::IngameType::Shoot, "");
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "Random Input Streamer:");
    ImGui::Checkbox("Auto-start on Ingame", &_autoSendRandomInput);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(120);
    ImGui::SliderInt("Interval (ms)", &_randomInputIntervalMs, 10, 200);

    bool isRandomActive = _dedicatedClient && _dedicatedClient->IsRandomInputActive();

    if (isRandomActive)
    {
        if (ImGui::Button("■ Stop Random Input Stream", ImVec2(240, 32)))
        {
            _dedicatedClient->StopRandomInput();
        }
    }
    else
    {
        if (ImGui::Button("▶ Start Random Input Stream", ImVec2(240, 32)))
        {
            _dedicatedClient->StartRandomInput(_randomInputIntervalMs);
        }
    }

    if (_dedicatedClient)
    {
        ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.6f, 1.0f), "Random Packets Streamed: %u", _dedicatedClient->GetRandomPacketsSent());
    }

    if (!isIngame)
        ImGui::EndDisabled();
}

void App::RenderLogPanel()
{
    ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "System & Network Logs");
    ImGui::SameLine();
    if (ImGui::SmallButton("Clear"))
    {
        std::lock_guard<std::mutex> lock(_logsMutex);
        _logs.clear();
    }
    ImGui::SameLine();
    ImGui::Checkbox("Auto-scroll", &_autoScroll);

    ImGui::BeginChild("LogConsoleRegion", ImVec2(0, 0), true, ImGuiWindowFlags_HorizontalScrollbar);
    {
        std::lock_guard<std::mutex> lock(_logsMutex);
        for (const auto& line : _logs)
        {
            if (line.find("error") != std::string::npos || line.find("Error") != std::string::npos || line.find("failed") != std::string::npos)
                ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "%s", line.c_str());
            else if (line.find("OK") != std::string::npos || line.find("success") != std::string::npos || line.find("Ready") != std::string::npos || line.find("Matched") != std::string::npos)
                ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.4f, 1.0f), "%s", line.c_str());
            else
                ImGui::TextUnformatted(line.c_str());
        }
        if (_autoScroll && ImGui::GetScrollY() >= ImGui::GetScrollMaxY())
            ImGui::SetScrollHereY(1.0f);
    }
    ImGui::EndChild();
}

void App::HttpRegister()
{
    std::string url = std::string(_authServerUrl) + "/auth/register";
    nlohmann::json bodyJson = {
        {"username", _username},
        {"password", _password}
    };

    AddLog(std::format("[HTTP] Registering user: {}", _username));
    try
    {
        auto r = cpr::Post(
            cpr::Url{url},
            cpr::Header{{"Content-Type", "application/json"}},
            cpr::Body{bodyJson.dump()},
            cpr::Timeout{3000}
        );

        if (r.status_code == 200 || r.status_code == 201)
        {
            AddLog(std::format("[HTTP] Registration successful (Status: {})", r.status_code));
        }
        else
        {
            AddLog(std::format("[HTTP] Registration response: {} - {}", r.status_code, r.text));
        }
    }
    catch (const std::exception& e)
    {
        AddLog(std::format("[HTTP] Register exception: {}", e.what()));
    }
}

void App::HttpLogin()
{
    std::string url = std::string(_authServerUrl) + "/auth/login";
    nlohmann::json bodyJson = {
        {"username", _username},
        {"password", _password}
    };

    AddLog(std::format("[HTTP] Logging in user: {}", _username));
    try
    {
        auto r = cpr::Post(
            cpr::Url{url},
            cpr::Header{{"Content-Type", "application/json"}},
            cpr::Body{bodyJson.dump()},
            cpr::Timeout{3000}
        );

        if (r.status_code == 200)
        {
            auto res = nlohmann::json::parse(r.text);
            if (res.contains("token"))
                _jwtToken = res["token"].get<std::string>();
            if (res.contains("userId"))
                _userId = res["userId"].get<std::string>();

            AddLog(std::format("[HTTP] Login Success! UserID: {}", _userId));
        }
        else
        {
            AddLog(std::format("[HTTP] Login failed: {} - {}", r.status_code, r.text));
        }
    }
    catch (const std::exception& e)
    {
        AddLog(std::format("[HTTP] Login exception: {}", e.what()));
    }
}

void App::HttpJoinQueue()
{
    if (_jwtToken.empty())
    {
        AddLog("[HTTP] Cannot join queue: Please login first!");
        return;
    }

    std::string url = std::string(_authServerUrl) + "/match/join";
    try
    {
        auto r = cpr::Post(
            cpr::Url{url},
            cpr::Header{{"Authorization", "Bearer " + _jwtToken}},
            cpr::Timeout{3000}
        );

        AddLog(std::format("[HTTP] Join queue status {}: {}", r.status_code, r.text));
        if (r.status_code == 200)
        {
            _matchStatus = "Waiting in Queue";
        }
    }
    catch (const std::exception& e)
    {
        AddLog(std::format("[HTTP] Join queue exception: {}", e.what()));
    }
}

void App::HttpCancelQueue()
{
    if (_jwtToken.empty())
        return;

    std::string url = std::string(_authServerUrl) + "/match/cancel";
    try
    {
        auto r = cpr::Post(
            cpr::Url{url},
            cpr::Header{{"Authorization", "Bearer " + _jwtToken}},
            cpr::Timeout{3000}
        );

        AddLog(std::format("[HTTP] Cancel queue status {}: {}", r.status_code, r.text));
        _matchStatus = "Cancelled";
    }
    catch (const std::exception& e)
    {
        AddLog(std::format("[HTTP] Cancel queue exception: {}", e.what()));
    }
}

void App::HttpCheckStatus()
{
    if (_jwtToken.empty())
    {
        AddLog("[HTTP] Cannot check status: Please login first!");
        return;
    }

    std::string url = std::string(_authServerUrl) + "/match/status";
    try
    {
        auto r = cpr::Get(
            cpr::Url{url},
            cpr::Header{{"Authorization", "Bearer " + _jwtToken}},
            cpr::Timeout{3000}
        );

        if (r.status_code == 200)
        {
            auto res = nlohmann::json::parse(r.text);
            if (res.contains("status"))
                _matchStatus = res["status"].get<std::string>();
            if (res.contains("matchId"))
                _matchId = res["matchId"].get<std::string>();
            if (res.contains("serverAddress"))
                _serverAddress = res["serverAddress"].get<std::string>();

            AddLog(std::format("[HTTP] Match Status: {}, Server: {}", _matchStatus, _serverAddress));

            // Parse serverAddress (IP:Port) to fill dedicated target
            if (!_serverAddress.empty() && _serverAddress != "pending")
            {
                auto colon = _serverAddress.find(':');
                if (colon != std::string::npos)
                {
                    std::string host = _serverAddress.substr(0, colon);
                    int port = std::stoi(_serverAddress.substr(colon + 1));
                    strncpy_s(_targetHost, sizeof(_targetHost), host.c_str(), _TRUNCATE);
                    _targetPort = port;
                    AddLog(std::format("[Client] Auto-filled Target Server: {}:{}", host, port));
                }
            }
        }
        else
        {
            AddLog(std::format("[HTTP] Check status: {} - {}", r.status_code, r.text));
        }
    }
    catch (const std::exception& e)
    {
        AddLog(std::format("[HTTP] Check status exception: {}", e.what()));
    }
}

void App::TriggerTenPlayerMatch()
{
    if (_isTriggeringMatch)
        return;

    _isTriggeringMatch = true;
    std::string baseUrl = _authServerUrl;
    std::string mainUser = _username;
    std::string mainPass = _password;

    _matchFuture = std::async(std::launch::async, [this, baseUrl, mainUser, mainPass]() {
        try
        {
            AddLog("[Trigger] 1/4: Ensuring primary user is logged in & joined...");
            if (_jwtToken.empty())
            {
                HttpRegister();
                HttpLogin();
            }

            HttpJoinQueue();

            auto timestamp = std::to_string(std::chrono::system_clock::now().time_since_epoch().count() % 1000000);
            AddLog(std::format("[Trigger] 2/4: Creating & joining 9 bot players (bot_{}_1..9)...", timestamp));

            for (int i = 1; i <= 9; ++i)
            {
                std::string botName = std::format("bot_{}_{}", timestamp, i);
                std::string botPass = "password123!";
                nlohmann::json botJson = {{"username", botName}, {"password", botPass}};

                // Register
                cpr::Post(cpr::Url{baseUrl + "/auth/register"},
                          cpr::Header{{"Content-Type", "application/json"}},
                          cpr::Body{botJson.dump()}, cpr::Timeout{2000});

                // Login
                auto loginRes = cpr::Post(cpr::Url{baseUrl + "/auth/login"},
                                          cpr::Header{{"Content-Type", "application/json"}},
                                          cpr::Body{botJson.dump()}, cpr::Timeout{2000});

                if (loginRes.status_code == 200)
                {
                    auto parsed = nlohmann::json::parse(loginRes.text);
                    std::string botToken = parsed["token"].get<std::string>();

                    // Join Queue
                    cpr::Post(cpr::Url{baseUrl + "/match/join"},
                              cpr::Header{{"Authorization", "Bearer " + botToken}},
                              cpr::Timeout{2000});

                    AddLog(std::format("[Trigger] Bot {} joined queue.", i));
                }
            }

            AddLog("[Trigger] 3/4: All 10 players queued! Waiting for AuthServer to spawn Dedicated Server...");
            std::this_thread::sleep_for(std::chrono::milliseconds(1000));

            AddLog("[Trigger] 4/4: Querying match result...");
            HttpCheckStatus();

            AddLog("[Trigger] Matchmaking cycle finished! Check Dedicated Server section to connect.");
        }
        catch (const std::exception& e)
        {
            AddLog(std::format("[Trigger] Exception during matchmaking: {}", e.what()));
        }

        _isTriggeringMatch = false;
    });
}

void App::ShowFinishBanner(const std::string& title, const std::string& details)
{
    {
        std::lock_guard<std::mutex> lock(_bannerMutex);
        _finishBannerTitle = title;
        _finishBannerDetails = details;
    }
    _triggerFinishModal = true;

    AddLog("========================================================");
    AddLog(std::format(" [MATCH RESULT BANNER] {}", title));
    AddLog(std::format(" {}", details));
    AddLog("========================================================");
}

void App::RenderFinishModal()
{
    if (_triggerFinishModal.exchange(false))
    {
        _isFinishModalOpen = true;
        ImGui::OpenPopup("Match Finished Notification");
    }

    if (!_isFinishModalOpen)
        return;

    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(520, 260));

    if (ImGui::BeginPopupModal("Match Finished Notification", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove))
    {
        std::string title;
        std::string details;
        {
            std::lock_guard<std::mutex> lock(_bannerMutex);
            title = _finishBannerTitle;
            details = _finishBannerDetails;
        }

        ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.4f, 1.0f), "=== %s ===", title.c_str());
        ImGui::Separator();
        ImGui::Spacing();

        ImGui::TextWrapped("%s", details.c_str());

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        if (ImGui::Button("Close Notification", ImVec2(150, 35)))
        {
            _isFinishModalOpen = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

void App::ConnectToDedicatedServerAuto()
{
    if (_serverAddress.empty() || _serverAddress == "pending")
    {
        AddLog("[Client] Target server address not ready yet.");
        return;
    }

    auto colon = _serverAddress.find(':');
    if (colon != std::string::npos)
    {
        std::string host = _serverAddress.substr(0, colon);
        int port = std::stoi(_serverAddress.substr(colon + 1));
        strncpy_s(_targetHost, sizeof(_targetHost), host.c_str(), _TRUNCATE);
        _targetPort = port;
        AddLog(std::format("[Client] Auto-connecting to Dedicated Server {}:{}...", host, port));
        _dedicatedClient->Connect(host, static_cast<uint16_t>(port));
    }
}

void App::RunAutomatedLifecycleTest()
{
    if (_isAutomatedTesting)
        return;

    _isAutomatedTesting = true;
    _triggerFinishModal = false;
    _isFinishModalOpen = false;

    _testFuture = std::async(std::launch::async, [this]() {
        try
        {
            AddLog("========================================================");
            AddLog("    STARTING AUTOMATED CLIENT E2E LIFECYCLE TEST        ");
            AddLog("========================================================");

            // 1. Ensure AuthServer login & 10 players matchmaking
            AddLog("[Test 1/5] Registering/logging in and queuing 10 players...");
            if (_jwtToken.empty())
            {
                HttpRegister();
                HttpLogin();
            }
            HttpJoinQueue();

            auto timestamp = std::to_string(std::chrono::system_clock::now().time_since_epoch().count() % 1000000);
            for (int i = 1; i <= 9; ++i)
            {
                std::string botName = std::format("test_bot_{}_{}", timestamp, i);
                std::string botPass = "password123!";
                nlohmann::json botJson = {{"username", botName}, {"password", botPass}};

                cpr::Post(cpr::Url{std::string(_authServerUrl) + "/auth/register"},
                          cpr::Header{{"Content-Type", "application/json"}},
                          cpr::Body{botJson.dump()}, cpr::Timeout{2000});

                auto loginRes = cpr::Post(cpr::Url{std::string(_authServerUrl) + "/auth/login"},
                                          cpr::Header{{"Content-Type", "application/json"}},
                                          cpr::Body{botJson.dump()}, cpr::Timeout{2000});

                if (loginRes.status_code == 200)
                {
                    auto parsed = nlohmann::json::parse(loginRes.text);
                    std::string botToken = parsed["token"].get<std::string>();
                    cpr::Post(cpr::Url{std::string(_authServerUrl) + "/match/join"},
                              cpr::Header{{"Authorization", "Bearer " + botToken}},
                              cpr::Timeout{2000});
                }
            }

            // 2. Wait for match to be created & Dedicated Server spawned
            AddLog("[Test 2/5] Waiting for match assignment from AuthServer...");
            int retry = 0;
            while (retry++ < 15)
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(1000));
                HttpCheckStatus();
                if (!_serverAddress.empty() && _serverAddress != "pending")
                    break;
            }

            if (_serverAddress.empty() || _serverAddress == "pending")
            {
                AddLog("[Test FAIL] Dedicated Server was not spawned within timeout!");
                _isAutomatedTesting = false;
                return;
            }

            // 3. Connect to Dedicated Server
            AddLog(std::format("[Test 3/5] Connecting client to Dedicated Server: {}...", _serverAddress));
            ConnectToDedicatedServerAuto();

            // Wait for Ingame Ready (Hole punching)
            int connectRetry = 0;
            while (connectRetry++ < 30 && (!_dedicatedClient || !_dedicatedClient->IsIngame()))
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(200));
            }

            if (!_dedicatedClient || !_dedicatedClient->IsIngame())
            {
                AddLog("[Test FAIL] Failed to achieve Ingame Ready state with server!");
                _isAutomatedTesting = false;
                return;
            }

            AddLog("[Test PASS] Client successfully connected & UDP hole punch authenticated!");

            // 4. Stream random inputs
            AddLog("[Test 4/5] Streaming random input packets (Movement & Shoot) for 3 seconds...");
            _dedicatedClient->StopRandomInput();
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            _dedicatedClient->StartRandomInput(50);
            std::this_thread::sleep_for(std::chrono::seconds(3));
            uint32_t sent = _dedicatedClient->GetRandomPacketsSent();
            _dedicatedClient->StopRandomInput();
            AddLog(std::format("[Test 4/5] Successfully sent {} random input packets!", sent));

            // 5. Verification & Final Results
            AddLog("[Test 5/5] Fetching final match results from AuthServer...");
            std::this_thread::sleep_for(std::chrono::milliseconds(1500));
            HttpCheckStatus();

            std::string summary = std::format("Match ID: {}\nServer Address: {}\nRandom Packets Sent: {}\nMatch Status: {}",
                                              _matchId, _serverAddress, sent, _matchStatus);

            ShowFinishBanner("AUTOMATED E2E TEST COMPLETED", summary);

            AddLog("========================================================");
            AddLog("    AUTOMATED CLIENT E2E TEST SUCCESSFULLY FINISHED!     ");
            AddLog("========================================================");
        }
        catch (const std::exception& e)
        {
            AddLog(std::format("[Test Exception] {}", e.what()));
        }

        _isAutomatedTesting = false;
    });
}