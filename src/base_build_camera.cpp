#include "base_build_camera.h"

#include <algorithm>
#include <cmath>

#include "raid_camera.h"

namespace
{
constexpr float kPointerDragThreshold{4.0F};

bool finite(Vec2 value) noexcept
{
    return std::isfinite(value.x) && std::isfinite(value.y);
}

Vec2 normalizedOrZero(Vec2 value) noexcept
{
    const float lengthSquared = value.x * value.x + value.y * value.y;
    if (!std::isfinite(lengthSquared) || lengthSquared <= 0.0F)
        return {};
    const float inverseLength = 1.0F / std::sqrt(lengthSquared);
    return {value.x * inverseLength, value.y * inverseLength};
}
}

void BaseBuildCameraController::activate(
    Vec2 focusWorldPosition,
    Vec2 worldSize,
    Vec2 viewportWorldSize) noexcept
{
    active_ = true;
    focusWorldPosition_ = focusWorldPosition;
    cancelPointer();
    constrain(worldSize, viewportWorldSize);
}

void BaseBuildCameraController::deactivate() noexcept
{
    active_ = false;
    focusWorldPosition_ = {};
    cancelPointer();
}

bool BaseBuildCameraController::active() const noexcept
{
    return active_;
}

Vec2 BaseBuildCameraController::offset(
    Vec2 worldSize,
    Vec2 viewportWorldSize) const noexcept
{
    if (!active_)
        return {};
    return raidCameraOffset(
        focusWorldPosition_, worldSize, viewportWorldSize);
}

bool BaseBuildCameraController::panKeyboard(
    Vec2 direction,
    float deltaTime,
    float screenUnitsPerSecond,
    float zoom,
    Vec2 worldSize,
    Vec2 viewportWorldSize) noexcept
{
    if (!active_ || !finite(direction) || !std::isfinite(deltaTime) ||
        !std::isfinite(screenUnitsPerSecond) || !std::isfinite(zoom) ||
        deltaTime <= 0.0F || screenUnitsPerSecond <= 0.0F || zoom <= 0.0F)
    {
        return false;
    }
    const Vec2 normalized = normalizedOrZero(direction);
    if (normalized.x == 0.0F && normalized.y == 0.0F)
        return false;

    const Vec2 before = offset(worldSize, viewportWorldSize);
    const float worldDistance = screenUnitsPerSecond * deltaTime / zoom;
    focusWorldPosition_.x += normalized.x * worldDistance;
    focusWorldPosition_.y += normalized.y * worldDistance;
    constrain(worldSize, viewportWorldSize);
    const Vec2 after = offset(worldSize, viewportWorldSize);
    return before.x != after.x || before.y != after.y;
}

void BaseBuildCameraController::beginPointer(Vec2 screenPosition) noexcept
{
    if (!active_ || !finite(screenPosition))
        return;
    pointerStart_ = screenPosition;
    pointerLast_ = screenPosition;
    pointerDragging_ = false;
}

bool BaseBuildCameraController::updatePointer(
    Vec2 screenPosition,
    float zoom,
    Vec2 worldSize,
    Vec2 viewportWorldSize) noexcept
{
    if (!active_ || !pointerStart_.has_value() ||
        !pointerLast_.has_value() || !finite(screenPosition) ||
        !std::isfinite(zoom) || zoom <= 0.0F)
    {
        return false;
    }

    if (!pointerDragging_)
    {
        const Vec2 total{
            screenPosition.x - pointerStart_->x,
            screenPosition.y - pointerStart_->y};
        if (total.x * total.x + total.y * total.y >=
            kPointerDragThreshold * kPointerDragThreshold)
        {
            pointerDragging_ = true;
        }
    }

    if (pointerDragging_)
    {
        focusWorldPosition_.x -= (screenPosition.x - pointerLast_->x) / zoom;
        focusWorldPosition_.y -= (screenPosition.y - pointerLast_->y) / zoom;
        constrain(worldSize, viewportWorldSize);
    }
    pointerLast_ = screenPosition;
    return pointerDragging_;
}

BaseBuildPointerRelease BaseBuildCameraController::endPointer() noexcept
{
    if (!pointerStart_.has_value())
        return BaseBuildPointerRelease::None;
    const BaseBuildPointerRelease result = pointerDragging_
        ? BaseBuildPointerRelease::Dragged
        : BaseBuildPointerRelease::Click;
    cancelPointer();
    return result;
}

void BaseBuildCameraController::cancelPointer() noexcept
{
    pointerStart_.reset();
    pointerLast_.reset();
    pointerDragging_ = false;
}

void BaseBuildCameraController::constrain(
    Vec2 worldSize,
    Vec2 viewportWorldSize) noexcept
{
    if (!active_ || !finite(worldSize) || !finite(viewportWorldSize) ||
        worldSize.x <= 0.0F || worldSize.y <= 0.0F ||
        viewportWorldSize.x <= 0.0F || viewportWorldSize.y <= 0.0F)
    {
        return;
    }

    const auto clampAxis = [](float focus, float world, float viewport)
    {
        if (world <= viewport)
            return world * 0.5F;
        const float half = viewport * 0.5F;
        return std::clamp(focus, half, world - half);
    };
    focusWorldPosition_.x = clampAxis(
        focusWorldPosition_.x, worldSize.x, viewportWorldSize.x);
    focusWorldPosition_.y = clampAxis(
        focusWorldPosition_.y, worldSize.y, viewportWorldSize.y);
}
