#include "App.hpp"
#include <memory>

int main(int argc, char* argv[])
{
    auto app = std::make_unique<App>();

    if (app->Init())
    {
        app->Run();
    }

    return 0;
}