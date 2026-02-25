#include "App.hpp"

int main() {
    App app;
    if (app.Init()) {
        app.Run();
    }
    return 0;
}
