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

[[nodiscard]] bool validRaidViewport(Rect viewport) noexcept;

// Inclusive pixel-center bounds of the full window, including blank space
// when a map is smaller than the viewport. Ballistics keep their world bounds.
[[nodiscard]] Rect raidReticleWorldBounds(
    Vec2 cameraOffset,
    Vec2 viewportSize) noexcept;
