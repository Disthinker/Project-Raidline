#include "raid_camera.h"
#include "item_definition.h"

#include <algorithm>
#include <cmath>

namespace
{
bool finite(Vec2 value) noexcept
{
    return std::isfinite(value.x) && std::isfinite(value.y);
}

float cameraAxis(float focus, float world, float viewport) noexcept
{
    if (!std::isfinite(focus) || !std::isfinite(world) ||
        !std::isfinite(viewport) || world <= viewport || viewport <= 0.0F)
    {
        return 0.0F;
    }
    return std::clamp(focus - viewport * 0.5F, 0.0F, world - viewport);
}
}

Vec2 raidCameraOffset(
    Vec2 focusWorldPosition,
    Vec2 worldSize,
    Vec2 viewportSize) noexcept
{
    return {
        cameraAxis(focusWorldPosition.x, worldSize.x, viewportSize.x),
        cameraAxis(focusWorldPosition.y, worldSize.y, viewportSize.y)};
}

Vec2 raidWorldToScreen(Vec2 worldPosition, Vec2 cameraOffset) noexcept
{
    return {worldPosition.x - cameraOffset.x,
            worldPosition.y - cameraOffset.y};
}

Vec2 raidScreenToWorld(Vec2 screenPosition, Vec2 cameraOffset) noexcept
{
    return {screenPosition.x + cameraOffset.x,
            screenPosition.y + cameraOffset.y};
}

bool validRaidViewport(Rect viewport) noexcept
{
    return finite(viewport.position) && finite(viewport.size) &&
        viewport.size.x >= 1.0F && viewport.size.y >= 1.0F;
}

Rect raidReticleWorldBounds(Vec2 cameraOffset, Vec2 viewportSize) noexcept
{
    if (!validRaidViewport(Rect{cameraOffset, viewportSize}))
    {
        return {};
    }
    return Rect{
        cameraOffset,
        {viewportSize.x - 1.0F, viewportSize.y - 1.0F}};
}

Vec2 WeaponOpticViewProjection::worldToScreen(Vec2 world) const noexcept
{
    return {(world.x - camera.x) * scale, (world.y - camera.y) * scale};
}

Vec2 WeaponOpticViewProjection::screenToWorld(Vec2 screen) const noexcept
{
    return {camera.x + screen.x / scale, camera.y + screen.y / scale};
}

Rect WeaponOpticViewProjection::worldBounds() const noexcept
{
    return {camera, {screenSize.x / scale, screenSize.y / scale}};
}

Rect WeaponOpticViewProjection::reticleBounds() const noexcept
{
    return {camera, {(screenSize.x - 1.0F) / scale, (screenSize.y - 1.0F) / scale}};
}

WeaponOpticViewProjection weaponOpticProjection(
    WeaponOpticMode mode, Vec2 playerCenter, Vec2 worldSize,
    Vec2 screenSize, Vec2 windowAimIntent, float transitionWeight) noexcept
{
    if (mode == WeaponOpticMode::None || !validRaidViewport({{}, screenSize}) ||
        !std::isfinite(transitionWeight) || transitionWeight <= 0.0F)
        return {raidCameraOffset(playerCenter, worldSize, screenSize),
                screenSize, 1.0F, 0.0F, 0.0F};

    const float weight = std::min(transitionWeight, 1.0F);
    const bool high = mode == WeaponOpticMode::High;
    const float scale = std::lerp(1.0F, high ? 0.90F : 0.80F, weight);
    const float shortSide = std::min(screenSize.x, screenSize.y);
    const Vec2 worldViewport{screenSize.x / scale, screenSize.y / scale};
    const Vec2 neutralCamera = raidCameraOffset(playerCenter, worldSize, worldViewport);
    const Vec2 neutralPlayerPixel{(playerCenter.x - neutralCamera.x) * scale,
                                  (playerCenter.y - neutralCamera.y) * scale};
    const Vec2 intent = finite(windowAimIntent) ? windowAimIntent : neutralPlayerPixel;
    const Vec2 bias{(intent.x - neutralPlayerPixel.x) * 2.0F / screenSize.x,
                    (intent.y - neutralPlayerPixel.y) * 2.0F / screenSize.y};
    const float amount = shortSide * (high ? 0.40F : 0.18F) * weight /
        (scale * std::max(1.0F, std::hypot(bias.x, bias.y)));
    const Vec2 focus{playerCenter.x + bias.x * amount, playerCenter.y + bias.y * amount};
    const Vec2 camera = raidCameraOffset(focus, worldSize, worldViewport);
    // SDL's world viewport uses integer coordinates. Quantize once, inside
    // the map clamp, so input and presentation use the same origin.
    return {{std::floor(camera.x), std::floor(camera.y)}, screenSize, scale,
            high ? shortSide * 0.27F : 0.0F, weight};
}
