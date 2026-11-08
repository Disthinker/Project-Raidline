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

enum class WeaponOpticMode;

// One logical-window transform shared by input, firing and SDL presentation.
struct WeaponOpticViewProjection
{
    Vec2 camera{};
    Vec2 screenSize{};
    float scale{1.0F};
    float lensRadius{};
    float transitionWeight{1.0F};

    [[nodiscard]] Vec2 worldToScreen(Vec2 world) const noexcept;
    [[nodiscard]] Vec2 screenToWorld(Vec2 screen) const noexcept;
    [[nodiscard]] Rect worldBounds() const noexcept;
    [[nodiscard]] Rect reticleBounds() const noexcept;
};

[[nodiscard]] WeaponOpticViewProjection weaponOpticProjection(
    WeaponOpticMode mode, Vec2 playerCenter, Vec2 worldSize,
    Vec2 screenSize, Vec2 windowAimIntent, float transitionWeight = 1.0F) noexcept;
