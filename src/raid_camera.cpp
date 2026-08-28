#include "raid_camera.h"

#include <algorithm>
#include <cmath>

namespace
{
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
