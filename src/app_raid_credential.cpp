#include "app.h"

void App::renderRaidCredentialDocuments()
{
    const auto &world = gameSession_.world();
    const auto &snapshot = world.credentialExtractionSnapshot();
    if (!snapshot || world.activeRaidSpaceId() != snapshot->definition.investigationSpaceId)
        return;
    const Vec2 point = snapshot->definition.investigationPoint;
    const Rect visibleBounds{{point.x - 12.0F, point.y - 10.0F}, {24.0F, 20.0F}};
    if (!world.raidBoundsObservable(visibleBounds)) return;

    // This is an observed interaction marker in the existing information layer,
    // not a collectible asset or a map reveal.
    const SDL_FRect paper{point.x - 12.0F, point.y - 10.0F, 24.0F, 20.0F};
    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer_, 170, 184, 149, 220);
    SDL_RenderFillRect(renderer_, &paper);
    SDL_SetRenderDrawColor(renderer_, 36, 55, 45, 255);
    for (int line = 0; line < 3; ++line)
        SDL_RenderLine(renderer_, paper.x + 4.0F, paper.y + 5.0F + line * 4.0F,
            paper.x + 19.0F, paper.y + 5.0F + line * 4.0F);
    const bool completed = snapshot->investigationCompleted || snapshot->unlockedAtDeploy;
    renderWorldLabel(point.x - 18.0F, point.y - 32.0F,
        snapshot->unlockedAtDeploy ? "TRANSIT PERMIT REGISTERED" :
        completed ? "TRANSIT DOCUMENTS SECURED" :
        world.credentialInvestigationInRange() ? "TRANSIT DOCUMENTS | HOLD F 2s" : "TRANSIT DOCUMENTS");
    const float progress = world.credentialInvestigationProgress();
    if (!completed && progress > 0.0F)
    {
        const SDL_FRect track{point.x - 24.0F, point.y + 17.0F, 48.0F, 5.0F};
        SDL_SetRenderDrawColor(renderer_, 26, 42, 33, 235);
        SDL_RenderFillRect(renderer_, &track);
        const SDL_FRect fill{track.x, track.y, track.w * progress, track.h};
        SDL_SetRenderDrawColor(renderer_, 156, 224, 174, 245);
        SDL_RenderFillRect(renderer_, &fill);
    }
    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_NONE);
}
