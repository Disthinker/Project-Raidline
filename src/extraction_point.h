#pragma once

#include "rect.h"

class ExtractionPoint
{
public:
    ExtractionPoint(
        Vec2 position,
        Vec2 size);

    [[nodiscard]]
    const Rect &bounds() const noexcept;

    // Uses a half-open rectangle so adjacent areas cannot both contain the
    // same point on a shared right or bottom edge.
    [[nodiscard]]
    bool contains(
        Vec2 point) const noexcept;

private:
    Rect bounds_;
};
