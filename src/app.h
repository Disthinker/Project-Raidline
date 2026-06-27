#pragma once

#include <SDL3/SDL.h>
#include <vector>
#include "input_system.h"
#include "gameplay_input.h"
#include "gameplay_world.h"
#include "projectile.h"
#include "enemy.h"

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

    std::vector<Projectile> projectiles_;

    std::vector<Enemy> enemies_;

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

    void shutdown();
};
