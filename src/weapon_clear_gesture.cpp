#include "weapon_clear_gesture.h"

#include <cmath>

namespace
{
constexpr float kMinimumSegment = 36.0F;
constexpr float kReversalWindow = 1.0F;
constexpr float kCosine120Degrees = -0.5F;

float lengthSquared(Vec2 value) noexcept
{
    return value.x * value.x + value.y * value.y;
}
}

bool WeaponClearGesture::observe(Vec2 delta, float nowSeconds) noexcept
{
    if (!std::isfinite(delta.x) || !std::isfinite(delta.y) ||
        !std::isfinite(nowSeconds) || lengthSquared(delta) < 1.0F)
    {
        return false;
    }
    while (!reversalTimes_.empty() &&
           nowSeconds - reversalTimes_.front() > kReversalWindow)
    {
        reversalTimes_.pop_front();
    }

    const float segmentLengthSquared = lengthSquared(segment_);
    const float deltaLengthSquared = lengthSquared(delta);
    if (segmentLengthSquared > 0.0F)
    {
        const float cosine =
            (segment_.x * delta.x + segment_.y * delta.y) /
            std::sqrt(segmentLengthSquared * deltaLengthSquared);
        if (segmentLengthSquared >= kMinimumSegment * kMinimumSegment &&
            cosine <= kCosine120Degrees)
        {
            reversalTimes_.push_back(nowSeconds);
            segment_ = delta;
            return reversalTimes_.size() >= 4U;
        }
    }
    segment_.x += delta.x;
    segment_.y += delta.y;
    return false;
}

void WeaponClearGesture::reset() noexcept
{
    segment_ = {};
    reversalTimes_.clear();
}

std::size_t WeaponClearGesture::reversalCount() const noexcept
{
    return reversalTimes_.size();
}
