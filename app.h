#pragma once

#include <SDL3/SDL.h>

class App{
public:
    int run();

private:
    SDL_Window* window_ {nullptr};
    SDL_Renderer* renderer_ {nullptr};
    bool running_ {false};

    bool initialize();
    void processEvents();
    void render();
    void shutdown();
};