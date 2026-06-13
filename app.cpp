// Implementation of the App class
#include <fmt/core.h>
#include "app.h"

// Init SDL video subsystem and create window
bool App::initialize(){
    if(!SDL_Init(SDL_INIT_VIDEO)){
        fmt::print("Init failed: {}\n", SDL_GetError());
        return false;
    }
    window_ = SDL_CreateWindow("Project Raidline", 1280, 720, 0);
    if(!window_){
        fmt::print("CreateWindow failed: {}\n", SDL_GetError());
        SDL_Quit();
        return false;
    }
    renderer_ = SDL_CreateRenderer(window_, nullptr);
    if(!renderer_){
        fmt::print("CreateRenderer failed: {}\n", SDL_GetError());
        SDL_DestroyWindow(window_);
        SDL_Quit();
        return false;
    }
    return true;
}

// Process SDL events, set running_ to false if quit event is received
void App::processEvents(){
    SDL_Event event;
    while(SDL_PollEvent(&event)){
        if(event.type == SDL_EVENT_QUIT){
            running_ = false;
        }
    }
}

// Render the window with a clear color
void App::render(){
    SDL_SetRenderDrawColor(renderer_, 18, 18, 24, 255);
    SDL_RenderClear(renderer_);
    SDL_RenderPresent(renderer_);
}

// Shutdown SDL and destroy window and renderer
void App::shutdown(){
    SDL_DestroyRenderer(renderer_);
    SDL_DestroyWindow(window_);
    SDL_Quit();
}

int App::run(){
    if(!initialize()){
        return 1;
    }

    running_ = true;

    while(running_){
        processEvents();
        render();
    }
    shutdown();
    return 0;
}