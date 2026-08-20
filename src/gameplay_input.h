#pragma once

#include <optional>

#include "vec2.h"

struct GameplayInput
{
    bool moveUp{};
    bool moveDown{};
    bool moveLeft{};
    bool moveRight{};
    bool sprint{};

    bool fireJustPressed{};
    bool firePressed{};

    // World-space pointer aim. Absence preserves the movement/previous facing
    // direction so the Space-key regression path remains available.
    std::optional<Vec2> aimWorldPosition;

    // 只在 F 从未按下变为按下的这一帧为 true。
    bool interactJustPressed{};

    bool reloadJustPressed{};
    bool healJustPressed{};
    bool quitRaidJustPressed{};
    bool inventoryOpen{};
    float movementSpeedMultiplier{1.0F};
};
