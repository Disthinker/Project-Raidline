// Implementation of the App class
#include <fmt/core.h>
#include "app.h"

// Init SDL video subsystem and create window
bool App::initialize(){
    if(!SDL_Init(SDL_INIT_VIDEO)){
        fmt::print("SDL_Init failed: {}\n", SDL_GetError());
        return false;
    }
    window_ = SDL_CreateWindow("Project Raidline", 1280, 720, 0);
    if(!window_){
        fmt::print("SDL_CreateWindow failed: {}\n", SDL_GetError());
        SDL_Quit();
        return false;
    }
    renderer_ = SDL_CreateRenderer(window_, nullptr);
    if(!renderer_){
        fmt::print("SDL_CreateRenderer failed: {}\n", SDL_GetError());
        SDL_DestroyWindow(window_);
        SDL_Quit();
        return false;
    }
    return true;
}

// Process SDL events, set running_ to false if quit event is received
void App::processEvents()
{
    SDL_Event event;
    while(SDL_PollEvent(&event)){
        input_.handleEvent(event);
        if(event.type == SDL_EVENT_QUIT){
            running_ = false;
        }
    }
}

void App::update(float deltaTime)
{
    player_.update(input_, deltaTime);
}

// Render the window with a clear color
void App::render()
{
    SDL_SetRenderDrawColor(renderer_, 18, 18, 24, 255);
    SDL_RenderClear(renderer_);

    SDL_SetRenderDrawColor(renderer_, 220, 220, 220, 255);
    if (input_.isActionPressed(GameAction::MoveUp)) {
        SDL_RenderDebugText(renderer_, 20.0f, 20.0f, "Action: MoveUp");
    } else if (input_.isActionPressed(GameAction::MoveDown)) {
        SDL_RenderDebugText(renderer_, 20.0f, 20.0f, "Action: MoveDown");
    } else if (input_.isActionPressed(GameAction::MoveLeft)) {
        SDL_RenderDebugText(renderer_, 20.0f, 20.0f, "Action: MoveLeft");
    } else if (input_.isActionPressed(GameAction::MoveRight)) {
        SDL_RenderDebugText(renderer_, 20.0f, 20.0f, "Action: MoveRight");
    } else if (input_.isActionPressed(GameAction::Fire)) {
        SDL_RenderDebugText(renderer_, 20.0f, 20.0f, "Action: Fire");
    } else if (input_.isActionPressed(GameAction::Dodge)) {
        SDL_RenderDebugText(renderer_, 20.0f, 20.0f, "Action: Dodge");
    } else {
        SDL_RenderDebugText(renderer_, 20.0f, 20.0f, "Action: None");
    }

    // 绘制玩家操控角色
    const Vec2 pos = player_.position();
    SDL_FRect playerRect {
        pos.x,
        pos.y,
        player_.size(),
        player_.size()
    };
    SDL_SetRenderDrawColor(renderer_, 80, 180, 120, 255);
    SDL_RenderFillRect(renderer_, &playerRect);

    SDL_RenderPresent(renderer_);
}

// Shutdown SDL and destroy window and renderer
void App::shutdown()
{
    SDL_DestroyRenderer(renderer_);
    renderer_ = nullptr;

    SDL_DestroyWindow(window_);
    window_ = nullptr;

    SDL_Quit();
}

int App::run(){
    if(!initialize()){
        return 1;
    }

    running_ = true;

    while(running_){
        processEvents();
        update(1.0f / 60.0f);
        render();
    }
    shutdown();
    return 0;
}