#include "App.hpp"
#include <memory>

// TODO : 클라이언트가 여러개가 연결이 되지 않고 진행 됨 수정 요

int main(int argc, char* argv[])
{
    auto app = std::make_unique<App>();

    if (app->Init())
    {
        app->Run();
    }

    return 0;
}