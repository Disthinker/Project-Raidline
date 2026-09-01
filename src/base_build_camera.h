#pragma once

#include <optional>

#include "vec2.h"

enum class BaseBuildPointerRelease
{
    None,
    Click,
    Dragged
};

// SDL applies RenderScale to both the viewport origin and draw coordinates.
// Keep the camera projection explicit so rendering and pointer hit testing use
// the same contract at every construction zoom level.
[[nodiscard]] Vec2 baseBuildViewportOrigin(
    Vec2 cameraOffset,
    float zoom,
    Vec2 screenOffset = {}) noexcept;
[[nodiscard]] Vec2 baseBuildWorldToScreen(
    Vec2 worldPosition,
    Vec2 cameraOffset,
    float zoom,
    Vec2 screenOffset = {}) noexcept;
[[nodiscard]] Vec2 baseBuildScreenToWorld(
    Vec2 screenPosition,
    Vec2 cameraOffset,
    float zoom,
    Vec2 screenOffset = {}) noexcept;

class BaseBuildCameraController
{
public:
    void activate(
        Vec2 focusWorldPosition,
        Vec2 worldSize,
        Vec2 viewportWorldSize) noexcept;
    void deactivate() noexcept;

    [[nodiscard]] bool active() const noexcept;
    [[nodiscard]] Vec2 offset(
        Vec2 worldSize,
        Vec2 viewportWorldSize) const noexcept;

    [[nodiscard]] bool panKeyboard(
        Vec2 direction,
        float deltaTime,
        float screenUnitsPerSecond,
        float zoom,
        Vec2 worldSize,
        Vec2 viewportWorldSize) noexcept;

    void beginPointer(Vec2 screenPosition) noexcept;
    [[nodiscard]] bool updatePointer(
        Vec2 screenPosition,
        float zoom,
        Vec2 worldSize,
        Vec2 viewportWorldSize) noexcept;
    [[nodiscard]] BaseBuildPointerRelease endPointer() noexcept;
    void cancelPointer() noexcept;

    void constrain(Vec2 worldSize, Vec2 viewportWorldSize) noexcept;

private:
    Vec2 focusWorldPosition_{};
    std::optional<Vec2> pointerStart_;
    std::optional<Vec2> pointerLast_;
    bool active_{};
    bool pointerDragging_{};
};
