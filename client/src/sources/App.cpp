#include "App.hpp"
#include "IOManager.hpp"
#include <cstdlib>
#include <ctime>

App::App()
{
}

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
    if(ImGui::TreeNode("Ingame Packets (Requires Room)"))
    {
        ImGui::Text("All Matched Clients:");
        if(ImGui::Button("Send Move##All"))
            SendIngamePacketsFromAll(IngameType::Move);
        ImGui::SameLine();
        if(ImGui::Button("Send Jump##All"))
            SendIngamePacketsFromAll(IngameType::Jump);
        ImGui::SameLine();
        if(ImGui::Button("Send Shoot##All"))
            SendIngamePacketsFromAll(IngameType::Shoot, static_cast<uint64_t>(_targetTick));
        ImGui::SameLine();
        if(ImGui::Button("Send Hit##All"))
            SendIngamePacketsFromAll(IngameType::Hit);

        ImGui::InputInt("Target Tick (Lag Comp)", &_targetTick);

        ImGui::Separator();
        ImGui::Checkbox("Auto-send Move (Stress)", &_autoSendIngame);
        if(_autoSendIngame)
        {
            _autoSendRandom = false;
            ImGui::SliderFloat("Interval (sec)", &_autoSendInterval, 0.01f, 5.0f);
        }

        ImGui::Checkbox("Auto-send Random Packets (Shoot, Move, Jump)", &_autoSendRandom);
        if(_autoSendRandom)
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

    ImGui::Text("Active Test Clients: %zu", [this]() {
        std::lock_guard<std::mutex> lock(_testClientsMutex);
        return _testClients.size();
    }());
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
    if(ImGui::BeginTable("ClientsTable", 6, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable))
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
            for(size_t i = 0; i < _testClients.size(); ++i)
            {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::Text("Test Client [%zu]", i);

                ImGui::TableSetColumnIndex(1);
                ImGui::TextColored(_testClients[i]->IsConnected() ? ImVec4(0, 1, 0, 1) : ImVec4(1, 0, 0, 1),
                                   _testClients[i]->IsConnected() ? "Connected" : "Disconnected");

                ImGui::TableSetColumnIndex(2);
                if(_testClients[i]->IsConnected())
                {
                    if(_testClients[i]->IsIngame())
                        ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "Ingame");
                    else if(!_testClients[i]->GetRoomId().empty())
                        ImGui::Text("Matched");
                    else if(_testClients[i]->IsMatching())
                        ImGui::Text("Matching...");
                    else
                        ImGui::Text("Wait...");
                }
                else { ImGui::Text("-"); }

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

    ImGui::Begin("Scoreboard");
    ImGui::BeginTable("Scores", 4, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable);

    ImGui::TableSetupColumn("PlayerId");
    ImGui::TableSetupColumn("Kill");
    ImGui::TableSetupColumn("Death");
    ImGui::TableSetupColumn("Heal");
    ImGui::TableHeadersRow();

    {
        std::lock_guard<std::mutex> lock(_testClientsMutex);
        if(!_testClients.empty())
        {
            auto scores = _testClients[0]->GetScores();

            for(const auto [id, score] : scores)
            {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::Text("%s", id.substr(0, 8).c_str());

                ImGui::TableSetColumnIndex(1);
                ImGui::Text("%s", std::to_string(score.kill()).c_str());

                ImGui::TableSetColumnIndex(2);
                ImGui::Text("%s", std::to_string(score.death()).c_str());

                ImGui::TableSetColumnIndex(3);
                ImGui::Text("%s", std::to_string(score.heal()).c_str());
            }
        }
    }

    ImGui::EndTable();

    ImGui::End();

    ImGui::Begin("World Visualization (2D Arena)");
    {
        std::shared_ptr<NetworkClient> activeClient = nullptr;
        {
            std::lock_guard<std::mutex> lock(_testClientsMutex);
            for(const auto& client : _testClients)
            {
                if(client->IsConnected() && client->IsIngame())
                {
                    activeClient = client;
                    break;
                }
            }
        }

        if(!activeClient)
        {
            ImGui::Text("No active Ingame clients.");
            ImGui::Text("Please connect test clients and wait for matchmaking.");
        }
        else
        {
            ImGui::Text("Visualizing Room: %s", activeClient->GetRoomId().substr(0, 8).c_str());
            ImGui::Text("Reference Client: %s", activeClient->GetSessionId().substr(0, 8).c_str());

            auto players = activeClient->GetRoomPlayers();
            ImGui::Text("Players In Room: %zu", players.size());

            if(players.empty())
            {
                ImGui::Text("Waiting for player coordinate updates from server...");
            }
            else
            {
                if(ImGui::TreeNode("Player Coordinates (Debug)"))
                {
                    for(const auto& p : players)
                    {
                        ImGui::Text("[%s]: Pos(%.2f, %.2f, %.2f) | HP: %d | K/D: %d/%d",
                                    p.id.substr(0, 8).c_str(), p.x, p.y, p.z, p.hp, p.kills, p.deaths);
                    }
                    ImGui::TreePop();
                }

                // Draw 2D Minimap Canvas
                ImVec2 canvas_pos = ImGui::GetCursorScreenPos();
                ImVec2 canvas_size = ImVec2(400.0f, 400.0f);

                ImDrawList* draw_list = ImGui::GetWindowDrawList();
                // Draw background
                draw_list->AddRectFilled(canvas_pos, ImVec2(canvas_pos.x + canvas_size.x, canvas_pos.y + canvas_size.y), IM_COL32(30, 30, 45, 255));
                draw_list->AddRect(canvas_pos, ImVec2(canvas_pos.x + canvas_size.x, canvas_pos.y + canvas_size.y), IM_COL32(100, 100, 150, 255), 0.0f, 0, 2.0f);

                // Calculate bounds
                float minX = 9999.0f, maxX = -9999.0f;
                float minZ = 9999.0f, maxZ = -9999.0f;
                for(const auto& p : players)
                {
                    if(p.x < minX)
                        minX = p.x;
                    if(p.x > maxX)
                        maxX = p.x;
                    if(p.z < minZ)
                        minZ = p.z;
                    if(p.z > maxZ)
                        maxZ = p.z;
                }

                float rangeX = maxX - minX;
                float rangeZ = maxZ - minZ;
                if(rangeX < 50.0f)
                {
                    float midX = (minX + maxX) * 0.5f;
                    minX = midX - 25.0f;
                    maxX = midX + 25.0f;
                    rangeX = 50.0f;
                }
                else
                {
                    minX -= rangeX * 0.1f;
                    maxX += rangeX * 0.1f;
                    rangeX = maxX - minX;
                }

                if(rangeZ < 50.0f)
                {
                    float midZ = (minZ + maxZ) * 0.5f;
                    minZ = midZ - 25.0f;
                    maxZ = midZ + 25.0f;
                    rangeZ = 50.0f;
                }
                else
                {
                    minZ -= rangeZ * 0.1f;
                    maxZ += rangeZ * 0.1f;
                    rangeZ = maxZ - minZ;
                }

                auto now = std::chrono::steady_clock::now();

                // 1. Draw shooting lines first (so they draw behind player circles)
                for(const auto& p : players)
                {
                    if(p.isShooting)
                    {
                        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - p.shootTime).count();
                        if(elapsed < 500)
                        {
                            int alpha = static_cast<int>(255.0f * (1.0f - static_cast<float>(elapsed) / 500.0f));

                            // Map shooter screen coords
                            float sx = canvas_pos.x + ((p.x - minX) / rangeX) * canvas_size.x;
                            float sy = canvas_pos.y + ((p.z - minZ) / rangeZ) * canvas_size.y;

                            // Map endpoint
                            float endX = p.x + p.shootDir[0] * 80.0f;
                            float endZ = p.z + p.shootDir[2] * 80.0f;
                            float esx = canvas_pos.x + ((endX - minX) / rangeX) * canvas_size.x;
                            float esy = canvas_pos.y + ((endZ - minZ) / rangeZ) * canvas_size.y;

                            draw_list->AddLine(ImVec2(sx, sy), ImVec2(esx, esy), IM_COL32(255, 255, 0, alpha), 2.5f);
                            draw_list->AddCircleFilled(ImVec2(sx, sy), 5.0f, IM_COL32(255, 100, 0, alpha));
                        }
                    }
                }

                // 2. Draw players
                for(const auto& p : players)
                {
                    float sx = canvas_pos.x + ((p.x - minX) / rangeX) * canvas_size.x;
                    float sy = canvas_pos.y + ((p.z - minZ) / rangeZ) * canvas_size.y;

                    ImU32 color;
                    if(p.hp <= 0)
                    {
                        color = IM_COL32(200, 50, 50, 255); // Dead: Red
                    }
                    else if(p.id == activeClient->GetSessionId())
                    {
                        color = IM_COL32(50, 220, 50, 255); // Client itself: Green
                    }
                    else
                    {
                        color = IM_COL32(50, 150, 250, 255); // Other players: Light Blue
                    }

                    float radius = 8.0f;
                    if(p.y > 0.0f)
                    {
                        // Increase radius and draw shadow for jump altitude visual effect
                        radius += p.y * 1.2f;
                        draw_list->AddCircle(ImVec2(sx, sy), radius + 4.0f, IM_COL32(255, 255, 0, 180), 0, 1.5f);
                    }

                    // Draw player base
                    draw_list->AddCircleFilled(ImVec2(sx, sy), radius, color);
                    draw_list->AddCircle(ImVec2(sx, sy), radius, IM_COL32(255, 255, 255, 200), 0, 1.0f);

                    // Draw Health Bar
                    float hpBarY = sy - radius - 8.0f;
                    draw_list->AddRectFilled(ImVec2(sx - 12.0f, hpBarY), ImVec2(sx + 12.0f, hpBarY + 3.0f), IM_COL32(150, 50, 50, 255));
                    float hpRatio = static_cast<float>(p.hp) / 100.0f;
                    if(hpRatio > 0.0f)
                    {
                        if(hpRatio > 1.0f)
                            hpRatio = 1.0f;
                        draw_list->AddRectFilled(ImVec2(sx - 12.0f, hpBarY), ImVec2(sx - 12.0f + 24.0f * hpRatio, hpBarY + 3.0f), IM_COL32(50, 220, 50, 255));
                    }

                    // Draw short ID Label
                    std::string label = p.id.substr(0, 4);
                    draw_list->AddText(ImVec2(sx - 12.0f, sy + radius + 2.0f), IM_COL32(220, 220, 220, 255), label.c_str());

                    // 3. Draw hit visual indicator
                    if(p.isHit)
                    {
                        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - p.hitTime).count();
                        if(elapsed < 300)
                        {
                            int alpha = static_cast<int>(255.0f * (1.0f - static_cast<float>(elapsed) / 300.0f));
                            draw_list->AddCircle(ImVec2(sx, sy), radius + 6.0f, IM_COL32(255, 0, 0, alpha), 0, 2.5f);
                            draw_list->AddText(ImVec2(sx - 15.0f, hpBarY - 14.0f), IM_COL32(255, 50, 50, alpha), "HIT!");
                        }
                    }
                }

                ImGui::Dummy(canvas_size); // Reserve canvas layout space in ImGui window
            }
        }
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

            client->Connect(_host, static_cast<uint16_t>(_port));

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

    if(_ioManager)
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
    for(auto& client : _testClients)
    {
        if(client->IsConnected() && !client->GetRoomId().empty())
        {
            uint64_t actualTick = (clientTick == 0) ? client->GetLastServerTick() : clientTick;

            if(type == IngameType::Move)
            {
                struct MoveData
                {
                    float dx = 1.0f;
                    float dy = 0.0f;
                    float dz = 0.0f;
                    std::int32_t speed = 10;
                } data;
                std::string sendData(sizeof(MoveData), '\0');
                std::memcpy(&sendData[0], &data, sizeof(MoveData));
                client->SendIngamePacket(type, sendData, actualTick);
            }
            else if(type == IngameType::Shoot)
            {
                struct ShootData
                {
                    float x = 1.0f;
                    float y = 0.0f;
                    float z = 0.0f;
                } data;
                std::string sendData(sizeof(ShootData), '\0');
                std::memcpy(&sendData[0], &data, sizeof(ShootData));
                client->SendIngamePacket(type, sendData, actualTick);
            }
            else
            {
                client->SendIngamePacket(type, "TestClientPacket", actualTick);
            }
        }
    }
}

void App::UpdateAutoSend()
{
    if(!_autoSendIngame && !_autoSendRandom)
        return;

    double currentTime = glfwGetTime();
    if(currentTime - _lastAutoSendTime >= _autoSendInterval)
    {
        if(_autoSendIngame)
        {
            SendIngamePacketsFromAll(IngameType::Move);
        }
        else if(_autoSendRandom)
        {
            std::lock_guard<std::mutex> lock(_testClientsMutex);
            for(auto& client : _testClients)
            {
                if(client->IsConnected() && client->IsIngame())
                {
                    int packetType = rand() % 3; // 0: Move, 1: Jump, 2: Shoot
                    if(packetType == 0)
                    {
                        struct MoveData
                        {
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
                    else if(packetType == 1)
                    {
                        client->SendIngamePacket(IngameType::Jump, "");
                    }
                    else if(packetType == 2)
                    {
                        struct ShootData
                        {
                            float x = 0.0f;
                            float y = 0.0f;
                            float z = 1.0f;
                        } data;
                        data.x = static_cast<float>(rand()) / static_cast<float>(RAND_MAX) * 2.0f - 1.0f;
                        data.z = static_cast<float>(rand()) / static_cast<float>(RAND_MAX) * 2.0f - 1.0f;

                        std::string sendData(sizeof(ShootData), '\0');
                        std::memcpy(&sendData[0], &data, sizeof(ShootData));

                        uint64_t actualTick = (_targetTick == 0) ? client->GetLastServerTick() : (uint64_t)_targetTick;
                        client->SendIngamePacket(IngameType::Shoot, sendData, actualTick);
                    }
                }
            }
        }
        _lastAutoSendTime = currentTime;
    }
}