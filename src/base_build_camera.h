#pragma once

#include <optional>

#include "vec2.h"

enum class BaseBuildPointerRelease
{
    None,
    Click,
    Dragged
};

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
