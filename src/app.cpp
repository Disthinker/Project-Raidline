// Implementation of the App class
#include "app.h"

#include <fmt/core.h>
#include <SDL3_image/SDL_image.h>
#include <string>
#include <utility>

namespace
{
    constexpr int kWindowWidth{1280};
    constexpr int kWindowHeight{720};

    constexpr int kPlayerSpriteWidth{64};
    constexpr int kPlayerSpriteHeight{80};
}

bool App::loadTextures()
{
    const char *basePath = SDL_GetBasePath();
    if (basePath == nullptr)
    {
        fmt::print("SDL_GetBasePath failed: {}\n", SDL_GetError());
        return false;
    }
    fmt::print("basePath: {}\n", basePath);
    const std::string assetRoot = std::string(basePath) + "assets/";
    const std::string backgroundPath =
        assetRoot + "backgrounds/project_raidline_test_map_1280x720.png";
    const std::string playerPath =
        assetRoot + "characters/protagonist_left_minimal_256x320.png";

    Texture backgroundTexture{IMG_LoadTexture(renderer_, backgroundPath.c_str())};
    if (!backgroundTexture.valid())
    {
        fmt::print("IMG_LoadTexture failed for background: {}\n", SDL_GetError());
        return false;
    }

    Texture playerTexture{IMG_LoadTexture(renderer_, playerPath.c_str())};
    if (!playerTexture.valid())
    {
        fmt::print("IMG_LoadTexture failed for player: {}\n", SDL_GetError());
        return false;
    }

    backgroundTexture_ = std::move(backgroundTexture);
    playerTexture_ = std::move(playerTexture);

    return true;
}

// Init SDL video subsystem and create window
bool App::initialize()
{
    if (!SDL_Init(SDL_INIT_VIDEO))
    {
        fmt::print("SDL_Init failed: {}\n", SDL_GetError());
        return false;
    }

    window_ = SDL_CreateWindow("Project Raidline", kWindowWidth, kWindowHeight, 0);
    if (!window_)
    {
        fmt::print("SDL_CreateWindow failed: {}\n", SDL_GetError());
        SDL_Quit();
        return false;
    }

    renderer_ = SDL_CreateRenderer(window_, nullptr);
    if (!renderer_)
    {
        fmt::print("SDL_CreateRenderer failed: {}\n", SDL_GetError());
        SDL_DestroyWindow(window_);
        SDL_Quit();
        return false;
    }

    if (!loadTextures())
    {
        fmt::print("loadTextures failed: {}\n", SDL_GetError());
        return false;
    }

    return true;
}

// 把输入状态翻译成 gameplay 输入
GameplayInput App::makeGameplayInput() const
{
    GameplayInput input{};
    input.moveUp = input_.isActionPressed(GameAction::MoveUp);
    input.moveDown = input_.isActionPressed(GameAction::MoveDown);
    input.moveLeft = input_.isActionPressed(GameAction::MoveLeft);
    input.moveRight = input_.isActionPressed(GameAction::MoveRight);
    input.fireJustPressed = input_.wasActionJustPressed(GameAction::Fire);
    input.firePressed = input_.isActionPressed(GameAction::Fire);
    return input;
}

// Process SDL events, set running_ to false if quit event is received
void App::processEvents()
{
    SDL_Event event;
    while (SDL_PollEvent(&event))
    {
        input_.handleEvent(event);
        if (event.type == SDL_EVENT_QUIT)
        {
            running_ = false;
        }
    }
}

void App::update(float deltaTime)
{
    const GameplayInput gameplayInput = makeGameplayInput();
    world_.update(gameplayInput, deltaTime);
}

void App::renderDebugText()
{
    SDL_SetRenderDrawColor(renderer_, 220, 220, 220, 255);
    if (input_.isActionPressed(GameAction::MoveUp))
    {
        SDL_RenderDebugText(renderer_, 20.0f, 20.0f, "Action: MoveUp");
    }
    else if (input_.isActionPressed(GameAction::MoveDown))
    {
        SDL_RenderDebugText(renderer_, 20.0f, 20.0f, "Action: MoveDown");
    }
    else if (input_.isActionPressed(GameAction::MoveLeft))
    {
        SDL_RenderDebugText(renderer_, 20.0f, 20.0f, "Action: MoveLeft");
    }
    else if (input_.isActionPressed(GameAction::MoveRight))
    {
        SDL_RenderDebugText(renderer_, 20.0f, 20.0f, "Action: MoveRight");
    }
    else if (input_.isActionPressed(GameAction::Fire))
    {
        SDL_RenderDebugText(renderer_, 20.0f, 20.0f, "Action: Fire");
    }
    else if (input_.isActionPressed(GameAction::Dodge))
    {
        SDL_RenderDebugText(renderer_, 20.0f, 20.0f, "Action: Dodge");
    }
    else
    {
        SDL_RenderDebugText(renderer_, 20.0f, 20.0f, "Action: None");
    }
}

void App::renderBackground()
{
    SDL_RenderTexture(renderer_, backgroundTexture_.get(), nullptr, nullptr);
}

void App::renderProjectiles()
{
    SDL_SetRenderDrawColor(renderer_, 255, 255, 255, 255);
    for (const auto &projectile : world_.projectiles())
    {
        const Vec2 pos = projectile.position();

        SDL_FRect rect{
            pos.x,
            pos.y,
            projectile.width(),
            projectile.height()};

        SDL_RenderFillRect(renderer_, &rect);
    }
}

void App::renderPlayer()
{
    const Player &player = world_.player();
    const Vec2 logicPos = player.position();
    const float logicSize = player.size();

    const float spriteW = kPlayerSpriteWidth;
    const float spriteH = kPlayerSpriteHeight;

    float spriteX = logicPos.x + (logicSize - spriteW) / 2;
    float spriteY = logicPos.y + (logicSize - spriteH) / 2;

    SDL_FRect playerRect{
        spriteX,
        spriteY,
        spriteW,
        spriteH};
    SDL_RenderTexture(renderer_, playerTexture_.get(), nullptr, &playerRect);
}

void App::renderEnemies()
{
    SDL_SetRenderDrawColor(renderer_, 180, 40, 40, 255);
    for (const auto &enemy : world_.enemies())
    {
        const Rect bounds = enemy.bounds();

        SDL_FRect rect{
            bounds.position.x,
            bounds.position.y,
            bounds.size.x,
            bounds.size.y};
        SDL_RenderFillRect(renderer_, &rect);
    }
}

void App::renderHitEffects()
{
    SDL_SetRenderDrawColor(renderer_, 255, 220, 80, 255);

    for (const auto &effect : world_.hitEffects())
    {
        const Vec2 center = effect.position();
        const float size = effect.size();
        const float halfSize = size / 2.0f;

        SDL_FRect rect{
            center.x - halfSize,
            center.y - halfSize,
            size,
            size};

        SDL_RenderFillRect(renderer_, &rect);
    }
}

// Renderer
void App::render()
{
    SDL_SetRenderDrawColor(renderer_, 0, 0, 0, 255); // 黑色背景
    SDL_RenderClear(renderer_);

    renderBackground();

    // 绘制敌人
    renderEnemies();

    // 绘制玩家操控角色
    renderPlayer();

    // 绘制投射物
    renderProjectiles();

    // 绘制击中效果
    renderHitEffects();

    // 绘制调试文本
    renderDebugText();

    SDL_RenderPresent(renderer_);
}

// Shutdown SDL and destroy window and renderer
void App::shutdown()
{
    backgroundTexture_.reset();
    playerTexture_.reset();

    SDL_DestroyRenderer(renderer_);
    renderer_ = nullptr;

    SDL_DestroyWindow(window_);
    window_ = nullptr;

    SDL_Quit();
}

int App::run()
{
    if (!initialize())
    {
        return 1;
    }

    running_ = true;
    lastCounter_ = SDL_GetPerformanceCounter();

    while (running_)
    {
        const Uint64 currentCounter = SDL_GetPerformanceCounter();
        const Uint64 frequency = SDL_GetPerformanceFrequency();

        const float deltaTime =
            static_cast<float>(currentCounter - lastCounter_) /
            static_cast<float>(frequency);
        lastCounter_ = currentCounter;

        processEvents();
        update(deltaTime);
        render();
        input_.endFrame();
    }
    shutdown();
    return 0;
}