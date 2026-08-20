#include "logical_ballistics.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

LogicalBallisticFlight::LogicalBallisticFlight(
    const ShotResolution &resolution)
    : shotId_{resolution.shotId},
      origin_{resolution.origin},
      currentPosition_{resolution.origin},
      direction_{resolution.direction},
      impactPosition_{resolution.impactPosition},
      speed_{std::sqrt(
          resolution.velocity.x * resolution.velocity.x +
          resolution.velocity.y * resolution.velocity.y)},
      collisionExtent_{resolution.collisionExtent},
      maximumDistance_{resolution.maximumDistance},
      damage_{resolution.damage}
{
    if (!resolution.accepted() ||
        !std::isfinite(speed_) || speed_ <= 0.0F ||
        !std::isfinite(maximumDistance_) || maximumDistance_ <= 0.0F)
    {
        throw std::invalid_argument{
            "LogicalBallisticFlight requires an accepted shot"};
    }
}

LogicalBallisticAdvance LogicalBallisticFlight::advance(
    float deltaTime) noexcept
{
    LogicalBallisticAdvance result{
        currentPosition_,
        currentPosition_,
        reachedImpact()};
    if (!std::isfinite(deltaTime) || deltaTime <= 0.0F ||
        result.reachedImpact)
    {
        return result;
    }

    const float remaining = maximumDistance_ - distanceTravelled_;
    const float requested = speed_ * deltaTime;
    const float travelled = !std::isfinite(requested)
                                ? remaining
                                : std::min(remaining, requested);
    distanceTravelled_ += travelled;

    if (distanceTravelled_ >= maximumDistance_)
    {
        distanceTravelled_ = maximumDistance_;
        currentPosition_ = impactPosition_;
    }
    else
    {
        currentPosition_ = Vec2{
            origin_.x + direction_.x * distanceTravelled_,
            origin_.y + direction_.y * distanceTravelled_};
    }

    result.end = currentPosition_;
    result.reachedImpact = reachedImpact();
    return result;
}

ShotId LogicalBallisticFlight::shotId() const noexcept
{
    return shotId_;
}

Vec2 LogicalBallisticFlight::origin() const noexcept
{
    return origin_;
}

Vec2 LogicalBallisticFlight::currentPosition() const noexcept
{
    return currentPosition_;
}

Vec2 LogicalBallisticFlight::direction() const noexcept
{
    return direction_;
}

Vec2 LogicalBallisticFlight::impactPosition() const noexcept
{
    return impactPosition_;
}

float LogicalBallisticFlight::speed() const noexcept
{
    return speed_;
}

float LogicalBallisticFlight::collisionExtent() const noexcept
{
    return collisionExtent_;
}

float LogicalBallisticFlight::distanceTravelled() const noexcept
{
    return distanceTravelled_;
}

float LogicalBallisticFlight::maximumDistance() const noexcept
{
    return maximumDistance_;
}

int LogicalBallisticFlight::damage() const noexcept
{
    return damage_;
}

bool LogicalBallisticFlight::reachedImpact() const noexcept
{
    return distanceTravelled_ >= maximumDistance_;
}
