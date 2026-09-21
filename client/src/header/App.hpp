#pragma once

#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>
#include <string>
#include <vector>
#include <memory>
#include <atomic>
#include <future>

#include "IOManager.hpp"
#include "DedicatedClient.hpp"

class App
{
public:
    App();
    ~App();

    bool Init();
    void Run();
    void Shutdown();

private:
    void RenderUI();
    void RenderAuthPanel();
    void RenderDedicatedPanel();
    void RenderLogPanel();

    void HttpRegister();
    void HttpLogin();
    void HttpJoinQueue();
    void HttpCancelQueue();
    void HttpCheckStatus();
    void TriggerTenPlayerMatch();
    void RunAutomatedLifecycleTest();
    void ConnectToDedicatedServerAuto();
    void RenderFinishModal();
    void ShowFinishBanner(const std::string& title, const std::string& details);

    void AddLog(const std::string& msg);

    GLFWwindow* _window = nullptr;

    // Networking
    std::shared_ptr<IOManager> _ioManager;
    std::shared_ptr<DedicatedClient> _dedicatedClient;

    // Auth Server state
    char _authServerUrl[256] = "http://localhost:8080";
    char _username[64] = "player1";
    char _password[64] = "password123!";
    std::string _jwtToken;
    std::string _userId;

    // Match status
    std::string _matchStatus = "Not In Queue";
    std::string _serverAddress = "";
    std::string _matchId = "";

    // Dedicated server target
    char _targetHost[128] = "127.0.0.1";
    int _targetPort = 0;

    // Async task handling for 10-player match
    std::atomic<bool> _isTriggeringMatch{ false };
    std::future<void> _matchFuture;

    // Automated Test State
    std::atomic<bool> _isAutomatedTesting{ false };
    std::future<void> _testFuture;

    // Random input streaming
    bool _autoSendRandomInput = true;
    int _randomInputIntervalMs = 50;

    // Match finished modal / banner
    std::atomic<bool> _triggerFinishModal{ false };
    bool _isFinishModalOpen = false;
    std::string _finishBannerTitle;
    std::string _finishBannerDetails;
    std::mutex _bannerMutex;

    // Logs
    std::vector<std::string> _logs;
    std::mutex _logsMutex;
    bool _autoScroll = true;
};