#pragma once

#include <SDL3/SDL.h>
#include "input_system.h"

class App{
public:
    int run();

private:
    SDL_Window* window_ {nullptr};
    SDL_Renderer* renderer_ {nullptr};
    InputSystem input_;

    bool running_ {false};

    bool initialize();
    void processEvents();
    void render();
    void shutdown();
};