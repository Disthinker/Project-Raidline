#include "raid_camera.h"

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
