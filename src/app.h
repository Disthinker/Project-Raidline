#pragma once

#include <SDL3/SDL.h>
#include "input_system.h"
#include "player.h"

class App{
public:
    int run();

private:
    SDL_Window* window_ {nullptr};
    SDL_Renderer* renderer_ {nullptr};
    InputSystem input_;
    Uint64 lastCounter_ {};

    bool running_ {false};
    Player player_ {640.0f, 360.0f};

    bool initialize();
    void processEvents();
    void update(float deltaTime);
    void render();
    void shutdown();

    void renderDebugText();
    void renderPlayer();
};