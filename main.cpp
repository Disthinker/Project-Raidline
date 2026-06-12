#include <SDL3/SDL.h>
#include <fmt/core.h>

int main()
{
    SDL_Window* window = nullptr;
    SDL_Renderer* renderer = nullptr;
    SDL_Event event;

    bool running = false;

    // 1. 初始化 SDL video 子系统
    if (!SDL_Init(SDL_INIT_VIDEO))
    {
        fmt::print("SDL_Init failed: {}\n", SDL_GetError());
        return 1;
    }

    // 2. 创建窗口
    window = SDL_CreateWindow("Project Raidline", 1280, 720, 0);
    if (window == nullptr)
    {
        fmt::print("SDL_CreateWindow failed: {}\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    // 3. 创建renderer
    renderer = SDL_CreateRenderer(window, nullptr);
    if (renderer == nullptr)
    {
        fmt::print("SDL_CreateRenderer failed: {}\n", SDL_GetError());
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    running = true;
    // 4. 主循环
    while(running)
    {
        // 处理事件
        while (SDL_PollEvent(&event))
        {
            if (event.type == SDL_EVENT_QUIT)
            {
                running = false;
            }
        }

        // 设置清屏颜色
        SDL_SetRenderDrawColor(renderer, 18, 18, 24, 255);
        SDL_RenderClear(renderer);
        SDL_RenderPresent(renderer);
    }

    // 5. 释放资源，注意顺序
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();

    return 0;
}