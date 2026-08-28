#pragma once

#include "rect.h"
#include "vec2.h"

[[nodiscard]] Vec2 raidCameraOffset(
    Vec2 focusWorldPosition,
    Vec2 worldSize,
    Vec2 viewportSize) noexcept;

[[nodiscard]] Vec2 raidWorldToScreen(
    Vec2 worldPosition,
    Vec2 cameraOffset) noexcept;

[[nodiscard]] Vec2 raidScreenToWorld(
    Vec2 screenPosition,
    Vec2 cameraOffset) noexcept;

// World-space rectangle in which the reticle center remains visibly inside
// the current viewport. The inset also protects against one-frame camera
// translation while input is being applied.
[[nodiscard]] Rect raidReticleWorldBounds(
    Vec2 cameraOffset,
    Vec2 worldSize,
    Vec2 viewportSize,
    float inset) noexcept;
