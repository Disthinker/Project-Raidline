#pragma once
#include "rect.h"

bool isCollision(const Rect &rect1, const Rect &rect2);

// Resolve one axis against one static obstacle. The actor bounds describe the
// current position, while desiredX/desiredY may lie beyond the obstacle. These
// swept checks stop at the first contact boundary instead of relying on the
// final position to overlap, so a long frame cannot tunnel through cover.
[[nodiscard]] float resolveHorizontalCollision(
    const Rect &actorBounds,
    float desiredX,
    const Rect &obstacleBounds) noexcept;

[[nodiscard]] float resolveVerticalCollision(
    const Rect &actorBounds,
    float desiredY,
    const Rect &obstacleBounds) noexcept;
