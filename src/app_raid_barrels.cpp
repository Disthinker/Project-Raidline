#include "app.h"
#include "raid_volatile_barrel.h"

#include <array>

namespace
{
void drawBarrelRim(SDL_Renderer *renderer, Vec2 center, float width, float height)
{
    const auto [x,y] = center;
    const float slope = width*.67F;
    const std::array<SDL_FPoint,7> rim{{
        {x-width,y}, {x-slope,y-height}, {x+slope,y-height}, {x+width,y},
        {x+slope,y+height}, {x-slope,y+height}, {x-width,y}}};
    SDL_RenderLines(renderer,rim.data(),static_cast<int>(rim.size()));
}
}

void App::renderRaidVolatileBarrels()
{
    const auto &world = gameSession_.world();
    if (!world.inOutdoorRaidSpace()) return;
    const auto &player = world.player();
    const Vec2 observer{player.position().x+player.size()*.5F, player.position().y+player.size()*.5F};
    for (const auto &barrel : world.volatileBarrels()) {
        if (!world.raidBoundsObservable(volatileBarrelBounds(barrel.center))) continue;
        const auto [x,y] = barrel.center;
        if (barrel.spent) {
            SDL_SetRenderDrawColor(renderer_,53,49,44,255);
            const SDL_FRect shell{x-11,y-1,22,10};
            SDL_RenderFillRect(renderer_,&shell);
            SDL_SetRenderDrawColor(renderer_,143,127,105,255);
            drawBarrelRim(renderer_,{x,y+7},11,2);
            const std::array<SDL_FPoint,7> broken{{
                {x-11,y+6},{x-10,y-5},{x-5,y-1},{x-2,y-6},{x+3,y},
                {x+9,y-3},{x+11,y+6}}};
            SDL_RenderLines(renderer_,broken.data(),static_cast<int>(broken.size()));
        } else {
            const SDL_FRect body{x-10,y-11,20,23};
            SDL_SetRenderDrawColor(renderer_,157,69,46,255);
            SDL_RenderFillRect(renderer_,&body);
            SDL_SetRenderDrawColor(renderer_,222,182,114,255);
            drawBarrelRim(renderer_,{x,y-10},10,3);
            drawBarrelRim(renderer_,{x,y+11},10,2);
            const SDL_FRect band{x-10,y-1,20,4};
            SDL_RenderFillRect(renderer_,&band);
            SDL_SetRenderDrawColor(renderer_,67,49,38,255);
            SDL_RenderLine(renderer_,x-2,y+2,x,y-1);
            SDL_RenderLine(renderer_,x,y-1,x+2,y+2);
        }
        const float dx = x-observer.x, dy = y-observer.y;
        if (dx*dx + dy*dy < 140.0F*140.0F)
            renderWorldLabel(x+15, y-18, barrel.spent ? "Spent fuel barrel" : "Fuel barrel");
    }
}
