#include "App.hpp"
#include "IOManager.hpp"

int main() {
    auto app = std::make_unique<App>();

    if (app->Init()) {
        app->Run();
    }

    return 0;
}
