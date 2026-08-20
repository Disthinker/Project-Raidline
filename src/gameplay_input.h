#pragma once

#include <optional>

#include "item_definition.h"
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
    bool aimDownSights{};
    bool forceMaximumWeaponSpread{};

    // World-space pointer aim. Absence preserves the movement/previous facing
    // direction so the Space-key regression path remains available.
    std::optional<Vec2> aimWorldPosition;

    // SDL relative mouse motion is an explicit per-frame delta. Simulation
    // tests and legacy callers may keep using the absolute position above.
    std::optional<Vec2> aimMotionDelta;

    // 只在 F 从未按下变为按下的这一帧为 true。
    bool interactJustPressed{};

    bool reloadJustPressed{};
    bool healJustPressed{};
    std::optional<EquipmentSlotKind> weaponSlotJustPressed;
    bool quitRaidJustPressed{};
    bool inventoryOpen{};
    float movementSpeedMultiplier{1.0F};
};
