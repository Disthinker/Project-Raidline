#pragma once

#include "gameplay_input.h"
#include "gameplay_world.h"
#include "input_system.h"
#include "texture.h"
#include <SDL3/SDL.h>

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

    Texture backgroundTexture_;
    Texture playerTexture_;

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
