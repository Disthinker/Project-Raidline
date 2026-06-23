#pragma once

#include <SDL3/SDL.h>
#include <vector>
#include "input_system.h"
#include "player.h"
#include "projectile.h"

class App
{
public:
    int run();

private:
    SDL_Window *window_{nullptr};
    SDL_Renderer *renderer_{nullptr};
    InputSystem input_;
    Uint64 lastCounter_{};

    bool running_{false};
    Player player_{640.0f, 360.0f};

    SDL_Texture *backgroundTexture_{};
    SDL_Texture *playerTexture_{};

    std::vector<Projectile> projectiles_;

    bool loadTextures();
    bool initialize();
    void processEvents();
    void update(float deltaTime);
    void render();
    void renderBackground();
    void renderDebugText();
    void renderPlayer();
    void renderProjectiles();
    void shutdown();
};