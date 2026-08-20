#include "weapon_aim.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace
{
    constexpr float kPi{3.14159265358979323846F};

    bool finitePositive(float value) noexcept
    {
        return std::isfinite(value) && value > 0.0F;
    }

    bool validDirection(Vec2 value) noexcept
    {
        const float lengthSquared = value.x * value.x + value.y * value.y;
        return std::isfinite(lengthSquared) && lengthSquared > 0.0F;
    }

    float angleDegrees(Vec2 direction) noexcept
    {
        return std::atan2(direction.y, direction.x) * 180.0F / kPi;
    }

    float wrapDegrees(float value) noexcept
    {
        while (value > 180.0F)
        {
            value -= 360.0F;
        }
        while (value < -180.0F)
        {
            value += 360.0F;
        }
        return value;
    }
}

WeaponAimState::WeaponAimState()
    : WeaponAimState{WeaponAimConfig{}}
{
}

WeaponAimState::WeaponAimState(WeaponAimConfig config)
    : config_{config}
{
    if (!finitePositive(config_.maximumFollowDegreesPerSecond) ||
        !std::isfinite(config_.recoilDegreesPerShot) ||
        config_.recoilDegreesPerShot < 0.0F ||
        !finitePositive(config_.aimDownSightsDurationSeconds) ||
        !finitePositive(config_.effectiveRange) ||
        !finitePositive(config_.maximumRange) ||
        config_.effectiveRange > config_.maximumRange)
    {
        throw std::invalid_argument{"WeaponAimConfig values are inconsistent"};
    }
}

void WeaponAimState::update(
    Vec2 desiredDirection,
    float desiredDistance,
    bool aimDownSights,
    float deltaTime) noexcept
{
    if (!validDirection(desiredDirection) ||
        !std::isfinite(desiredDistance) || desiredDistance < 0.0F ||
        !std::isfinite(deltaTime) || deltaTime < 0.0F)
    {
        return;
    }

    desiredAngleDegrees_ = angleDegrees(desiredDirection);
    aimDistance_ = desiredDistance;
    if (!initialized_)
    {
        trackedAngleDegrees_ = desiredAngleDegrees_;
        initialized_ = true;
    }
    else if (deltaTime > 0.0F)
    {
        const float difference = wrapDegrees(
            desiredAngleDegrees_ - trackedAngleDegrees_);
        const float maximumStep =
            config_.maximumFollowDegreesPerSecond * deltaTime;
        trackedAngleDegrees_ = wrapDegrees(
            trackedAngleDegrees_ +
            std::clamp(difference, -maximumStep, maximumStep));
    }

    if (deltaTime <= 0.0F)
    {
        return;
    }
    const float progressStep =
        deltaTime / config_.aimDownSightsDurationSeconds;
    aimDownSightsProgress_ = std::clamp(
        aimDownSightsProgress_ + (aimDownSights ? progressStep : -progressStep),
        0.0F,
        1.0F);
}

void WeaponAimState::applyShotRecoil() noexcept
{
    recoilOffsetDegrees_ = std::clamp(
        recoilOffsetDegrees_ + config_.recoilDegreesPerShot,
        -18.0F,
        18.0F);
}

Vec2 WeaponAimState::actualDirection() const noexcept
{
    const float angle =
        (trackedAngleDegrees_ + recoilOffsetDegrees_) * kPi / 180.0F;
    return Vec2{std::cos(angle), std::sin(angle)};
}

float WeaponAimState::aimDistance() const noexcept
{
    return aimDistance_;
}

float WeaponAimState::aimDownSightsProgress() const noexcept
{
    return aimDownSightsProgress_;
}

bool WeaponAimState::beyondMaximumRange() const noexcept
{
    return aimDistance_ > config_.maximumRange;
}

float WeaponAimState::rangeSpreadFactor() const noexcept
{
    if (aimDistance_ <= config_.effectiveRange)
    {
        return 0.0F;
    }
    const float span = config_.maximumRange - config_.effectiveRange;
    if (span <= 0.0F)
    {
        return 1.0F;
    }
    return std::clamp(
        (aimDistance_ - config_.effectiveRange) / span,
        0.0F,
        1.0F);
}

float WeaponAimState::damageMultiplier() const noexcept
{
    return beyondMaximumRange() ? 0.25F : 1.0F;
}

float WeaponAimState::recoilOffsetDegrees() const noexcept
{
    return recoilOffsetDegrees_;
}
