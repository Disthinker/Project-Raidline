#pragma once

#include <SDL3/SDL.h>
#include "gameplay_input.h"
#include "gameplay_world.h"
#include "input_system.h"

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
    GameplayWorld world_;

    SDL_Texture *backgroundTexture_{};
    SDL_Texture *playerTexture_{};

    bool loadTextures();
    bool initialize();
    GameplayInput makeGameplayInput() const;
    void processEvents();
    void update(float deltaTime);

    void render();
    void renderBackground();
    void renderDebugText();
    void renderPlayer();
    void renderProjectiles();
    void renderEnemies();
    void renderHitEffects();

    void shutdown();
};
