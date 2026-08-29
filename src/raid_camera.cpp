#include "raid_camera.h"

#include <algorithm>
#include <cmath>
#include <utility>

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

Rect raidReticleWorldBounds(
    Vec2 cameraOffset,
    Vec2 worldSize,
    Vec2 viewportSize,
    float outsideMargin) noexcept
{
    if (!finite(cameraOffset) || !finite(worldSize) ||
        !finite(viewportSize) || worldSize.x <= 0.0F ||
        worldSize.y <= 0.0F || viewportSize.x <= 0.0F ||
        viewportSize.y <= 0.0F || !std::isfinite(outsideMargin))
    {
        return {};
    }

    const float safeOutsideMargin = std::max(0.0F, outsideMargin);
    const auto axisBounds = [safeOutsideMargin](
                                float camera,
                                float world,
                                float viewport)
    {
        const float visibleStart = std::clamp(camera, 0.0F, world);
        const float visibleEnd = std::clamp(
            camera + viewport, visibleStart, world);
        const float minimum = std::max(
            0.0F, visibleStart - safeOutsideMargin);
        const float maximum = std::min(
            world, visibleEnd + safeOutsideMargin);
        return std::pair{minimum, maximum};
    };

    const auto [minimumX, maximumX] = axisBounds(
        cameraOffset.x, worldSize.x, viewportSize.x);
    const auto [minimumY, maximumY] = axisBounds(
        cameraOffset.y, worldSize.y, viewportSize.y);
    return Rect{
        {minimumX, minimumY},
        {maximumX - minimumX, maximumY - minimumY}};
}
