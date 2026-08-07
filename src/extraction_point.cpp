#include "extraction_point.h"

#include <cmath>
#include <stdexcept>

ExtractionPoint::ExtractionPoint(
    Vec2 position,
    Vec2 size)
    : bounds_{position, size}
{
    const float right =
        position.x + size.x;
    const float bottom =
        position.y + size.y;

    if (!std::isfinite(position.x) ||
        !std::isfinite(position.y) ||
        !std::isfinite(size.x) ||
        !std::isfinite(size.y) ||
        !std::isfinite(right) ||
        !std::isfinite(bottom) ||
        size.x <= 0.0F ||
        size.y <= 0.0F)
    {
        throw std::invalid_argument{
            "Extraction point geometry must be finite and positive"};
    }
}

const Rect &ExtractionPoint::bounds() const noexcept
{
    return bounds_;
}

bool ExtractionPoint::contains(
    Vec2 point) const noexcept
{
    if (!std::isfinite(point.x) ||
        !std::isfinite(point.y))
    {
        return false;
    }

    return point.x >= bounds_.position.x &&
           point.y >= bounds_.position.y &&
           point.x < bounds_.position.x + bounds_.size.x &&
           point.y < bounds_.position.y + bounds_.size.y;
}
