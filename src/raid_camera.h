#pragma once

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
