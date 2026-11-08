#include "app.h"

#include <algorithm>
#include <cmath>

#include "ui_weapon_optic_view.h"

namespace
{
bool selectFullTarget(SDL_Renderer *renderer, SDL_Texture *texture)
{
    return SDL_SetRenderTarget(renderer, texture) &&
        SDL_SetRenderScale(renderer, 1.0F, 1.0F) &&
        SDL_SetRenderViewport(renderer, nullptr) &&
        SDL_SetRenderClipRect(renderer, nullptr);
}
}

WeaponOpticMode App::visibleWeaponOptic() const noexcept
{
    if (!input_.isSecondaryPointerPressed() || !shouldCaptureWorldPointer() || opticAimBlockedUntilRelease_)
        return WeaponOpticMode::None;
    return gameFlow_.state() == GameFlowState::Base
        ? gameFlow_.baseWorld().activeWeaponOptic()
        : gameSession_.world().activeWeaponOptic();
}

const WeaponOpticViewProjection *App::visibleWeaponOpticView() const noexcept
{
    if (!shouldCaptureWorldPointer() || opticAimBlockedUntilRelease_ || selectedThrowable_)
        return nullptr;
    const auto &view = gameFlow_.state() == GameFlowState::Base
        ? gameFlow_.baseWorld().weaponOpticView()
        : gameSession_.world().weaponOpticView();
    return view && view->transitionWeight > 0.0F ? &*view : nullptr;
}

void App::updateWeaponOpticPointerGate()
{
    const bool rightHeld = input_.isSecondaryPointerPressed();
    if (!rightHeld)
        opticAimBlockedUntilRelease_ = false;
    if (!shouldCaptureWorldPointer() || selectedThrowable_)
    {
        opticAimBlockedUntilRelease_ = rightHeld;
        gameSession_.cancelWeaponOptics();
    }
}

bool App::renderWeaponOpticWorld(const WeaponOpticViewProjection &projection,
    bool inBase, Vec2 aim, Vec2 worldSize)
{
    if (!ensureIndoorObservationTargets())
        return false;
    SDL_Texture *target = SDL_GetRenderTarget(renderer_);
    SDL_Texture *information = indoorInformationLayer_.get();
    SDL_Texture *composite = indoorShadowMask_.get();
    struct Restore
    {
        SDL_Renderer *renderer;
        SDL_Texture *target, *information, *composite;
        ~Restore()
        {
            selectFullTarget(renderer, target);
            SDL_SetTextureColorMod(information, 255, 255, 255);
            SDL_SetTextureBlendMode(composite, SDL_BLENDMODE_BLEND);
            SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
        }
    } restore{renderer_, target, information, composite};

    const auto view = makeUiWeaponOpticView(projection, aim);
    const Rect visible = view.sceneBounds();
    const Rect worldBounds = projection.worldBounds();
    const bool high = projection.lensRadius > 0.0F;
    const bool opaquePeriphery = view.hasOpaquePeriphery();
    const auto &base = gameFlow_.baseWorld();
    const auto &raid = gameSession_.world();
    const bool needsObservation = inBase || (raid.isAlphaRaidWorld() &&
        (raid.lightConeObservationEnabled() || !raid.inOutdoorRaidSpace()));
    const SDL_Rect viewport{
        -static_cast<int>(projection.camera.x),
        -static_cast<int>(projection.camera.y),
        static_cast<int>(std::ceil(std::max(worldSize.x, worldBounds.size.x))),
        static_cast<int>(std::ceil(std::max(worldSize.y, worldBounds.size.y)))};
    const int clipX = static_cast<int>(std::floor(visible.position.x));
    const int clipY = static_cast<int>(std::floor(visible.position.y));
    const SDL_Rect sceneClip{clipX, clipY,
        static_cast<int>(std::ceil(visible.position.x + visible.size.x)) - clipX,
        static_cast<int>(std::ceil(visible.position.y + visible.size.y)) - clipY};
    const auto selectScene = [&]
    {
        return SDL_SetRenderScale(renderer_, projection.scale, projection.scale) &&
               SDL_SetRenderViewport(renderer_, &viewport) &&
               SDL_SetRenderClipRect(renderer_, opaquePeriphery ? &sceneClip : nullptr);
    };
    const auto drawScene = [&](bool terrainOnly, bool drawAvatar)
    {
        if (!selectScene())
            return false;
        if (inBase)
            renderBaseWorldScene(visible, terrainOnly, drawAvatar);
        else
            renderRaidOpticScene(visible, terrainOnly, drawAvatar);
        return true;
    };
    const auto drawOwnAvatar = [&]
    {
        if (!selectScene())
            return false;
        if (inBase)
            renderPlayerAvatar(base.playerPosition(), base.playerSize(),
                base.playerFacingDirection(), base.playerIsMoving(), base.playerAnimationFrame());
        else
            renderPlayer();
        return true;
    };

    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_NONE);
    if (opaquePeriphery)
    {
        if (!selectFullTarget(renderer_, target))
            return false;
        SDL_SetRenderDrawColor(renderer_, 0, 0, 0, 255);
        if (!SDL_RenderClear(renderer_))
            return false;
    }
    if (!selectFullTarget(renderer_, information))
        return false;
    SDL_SetRenderDrawColor(renderer_, 17, 20, 18, 255);
    if (!SDL_RenderClear(renderer_))
        return false;

    // Ordinary outdoor raids without observation keep their normal scene
    // visibility until the optic's gameplay observation is active.
    if (!needsObservation)
    {
        return drawScene(false, true) && selectFullTarget(renderer_, target) &&
            drawUiWeaponOpticLens(renderer_, information, view);
    }

    if (!drawScene(true, false) || !selectFullTarget(renderer_, composite))
        return false;
    SDL_SetTextureColorMod(information, 62, 62, 62);
    if (!SDL_RenderTexture(renderer_, information, nullptr, nullptr))
        return false;
    SDL_SetTextureColorMod(information, 255, 255, 255);

    const auto paintBackgroundLight = [&](const auto &observations)
    {
        for (const auto &near : observations)
            if (!drawUiWeaponOpticObservation(renderer_, information, view, near, 0.36F))
                return false;
        return true;
    };
    if (!(inBase ? paintBackgroundLight(base.weaponLightBackgroundObservations())
                 : paintBackgroundLight(raid.weaponLightBackgroundObservations())))
        return false;

    if (!selectFullTarget(renderer_, information) ||
        !drawScene(false, opaquePeriphery) ||
        !selectFullTarget(renderer_, composite))
        return false;
    const auto &observation = inBase ? base.observation() : raid.raidObservation();
    if (!drawUiWeaponOpticObservation(renderer_, information, view, observation))
        return false;

    // The ordinary avatar participates in the high scope's peripheral fade.
    if (high && !opaquePeriphery && !drawOwnAvatar())
        return false;
    if (!selectFullTarget(renderer_, target) ||
        !SDL_SetTextureBlendMode(composite, SDL_BLENDMODE_NONE) ||
        !drawUiWeaponOpticLens(renderer_, composite, view))
        return false;

    // Low magnification always shows the player's entire avatar.
    return high || drawOwnAvatar();
}

void App::renderRaidOpticScene(Rect visible, bool terrainOnly, bool drawAvatar)
{
    renderBackground(true, terrainOnly, visible);
    renderBallisticBlockers(terrainOnly, visible);
    if (terrainOnly)
        return;
    renderExtractionPoint();
    renderRaidSpacePortal();
    if (gameSession_.world().isAlphaRaidWorld())
        renderAlphaRaidLoot();
    else
    {
        renderStorageCabinet();
        renderGroundItems();
    }
    renderEnemyAttackTelegraphs();
    renderEnemies();
    if (drawAvatar)
        renderPlayer();
    renderMeleeSwing(gameSession_.world().melee());
    renderShotPresentations();
    renderShotFeedbackPresentations();
    renderParticles();
    renderNoiseDecoyWorld();
}

void App::clearUnavailableOpticView()
{
    SDL_SetRenderDrawColor(renderer_, 17, 20, 18, 255);
    SDL_RenderClear(renderer_);
    uiMessage_ = "RAID OBSERVATION UNAVAILABLE";
}
