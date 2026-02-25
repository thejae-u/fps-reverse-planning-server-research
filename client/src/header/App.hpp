#pragma once

#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include "NetworkClient.hpp"
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>
#include <spdlog/spdlog.h>

class App {
public:
    App();
    ~App();

    bool Init();
    void Run();
    void Shutdown();

private:
    void RenderUI();

    GLFWwindow* _window = nullptr;
    NetworkClient _networkClient;

    char _host[128] = "127.0.0.1";
    int _port = 52800;
};
