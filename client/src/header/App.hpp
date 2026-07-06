#pragma once

#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include "NetworkClient.hpp"
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>
#include <spdlog/spdlog.h>
#include <vector>
#include <string>
#include <mutex>
#include <uuid.h>

class IOManager;

class App {
public:
    App();
    ~App();

    bool Init();
    void Run();
    void Shutdown();

private:
    void RenderUI();
    void OnMessage(const std::string& message);
    void StartMatchmakingTest(int count);
    void StopMatchmakingTest();
    void SendIngamePacketsFromAll(IngameType type, uint64_t clientTick = 0);
    void UpdateAutoSend();

    GLFWwindow* _window = nullptr;
    std::shared_ptr<IOManager> _ioManager;
    std::vector<std::shared_ptr<NetworkClient>> _testClients;
    std::mutex _testClientsMutex;


    char _host[128] = "127.0.0.1";
    int _port = 52800;
    int _udpPort = 52801;
    int _testClientCount = 10;
    int _udpPacketSize = 64;
    int _targetTick = 0;

    char _messageToSend[256] = "Test Message";
    char _udpMessageToSend[256] = "UDP Test Message";
    std::vector<std::string> _receivedMessages;
    std::mutex _messagesMutex;
    bool _useUdpForTest = false;
    bool _autoSendIngame = false;
    bool _autoSendRandom = false;
    float _autoSendInterval = 1.0f;
    double _lastAutoSendTime = 0.0;
};
