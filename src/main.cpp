#include "app/App.h"
#include <iostream>
#include <string>

// Entry point of the WASM application
int main(int argc, char** argv) {
    int width = 640;
    int height = 480;

    std::string markerPath = "/assets/markers/pinball";

    static App app;

    std::cout << "Initializing WebAR-Canvas with NFT..." << std::endl;

    if (!app.init("canvas", width, height, markerPath)) {
        std::cerr << "Error during ARBaseEngine initialization" << std::endl;
        return 1;
    }

    app.run();

    return 0;
}
