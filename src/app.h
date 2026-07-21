#pragma once

#include <array>

#include <SDL3/SDL.h>

#include "gameplay_input.h"
#include "gameplay_world.h"
#include "input_system.h"
#include "item_definition.h"
#include "texture.h"

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
    Texture playerMoveHorizontalTexture_;
    Texture enemyMoveHorizontalTexture_;

    // 每种 ItemId 对应一个由 App 独占的世界 Texture。
    // ItemDefinition 只保存资源路径，不拥有渲染资源。
    std::array<Texture, itemCount()>
        itemTextures_{};

    bool loadTextures();
    bool initialize();

    GameplayInput makeGameplayInput() const;

    void processEvents();
    void update(float deltaTime);

    void render();
    void renderBackground();
    void renderGroundItems();
    void renderEnemies();
    void renderPlayer();
    void renderProjectiles();
    void renderParticles();
    void renderDebugText();

    void shutdown();
};