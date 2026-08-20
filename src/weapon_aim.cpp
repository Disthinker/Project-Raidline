#include "weapon_aim.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace
{
    constexpr float kMaximumAimStep{1.0F / 120.0F};
    constexpr float kVelocityEpsilon{0.01F};

    bool finite(Vec2 value) noexcept
    {
        return std::isfinite(value.x) && std::isfinite(value.y);
    }

    bool finitePositive(float value) noexcept
    {
        return std::isfinite(value) && value > 0.0F;
    }

    float length(Vec2 value) noexcept
    {
        return std::sqrt(value.x * value.x + value.y * value.y);
    }

    Vec2 normalizedOr(Vec2 value, Vec2 fallback) noexcept
    {
        const float magnitude = length(value);
        if (!std::isfinite(magnitude) || magnitude <= 0.000001F)
        {
            return fallback;
        }
        return Vec2{value.x / magnitude, value.y / magnitude};
    }

    Vec2 moveTowards(
        Vec2 current,
        Vec2 target,
        float maximumDelta) noexcept
    {
        const Vec2 difference{
            target.x - current.x,
            target.y - current.y};
        const float distance = length(difference);
        if (!std::isfinite(distance) || distance <= maximumDelta ||
            distance <= 0.000001F)
        {
            return target;
        }
        return Vec2{
            current.x + difference.x / distance * maximumDelta,
            current.y + difference.y / distance * maximumDelta};
    }

    void validateConfig(const WeaponAimConfig &config)
    {
        if (!finitePositive(config.maximumReticleSpeed) ||
            !finitePositive(config.controlAcceleration) ||
            !std::isfinite(config.recoilInitialSpeed) ||
            config.recoilInitialSpeed < 0.0F ||
            !finitePositive(config.recoilDeceleration) ||
            !std::isfinite(config.recoilLateralRatio) ||
            config.recoilLateralRatio < 0.0F ||
            config.recoilLateralRatio > 1.0F ||
            !finitePositive(config.aimDownSightsDurationSeconds) ||
            !finitePositive(config.effectiveRange) ||
            !finitePositive(config.maximumRange) ||
            config.effectiveRange > config.maximumRange)
        {
            throw std::invalid_argument{
                "WeaponAimConfig values are inconsistent"};
        }
    }
}

WeaponAimState::WeaponAimState()
    : WeaponAimState{WeaponAimConfig{}}
{
}

WeaponAimState::WeaponAimState(WeaponAimConfig config)
    : config_{config},
      recoilRandom_{config.recoilSeed, 0x7265636f696c2d76ULL}
{
    validateConfig(config_);
}

void WeaponAimState::update(
    Vec2 targetWorldPosition,
    Vec2 shootingOrigin,
    Vec2 worldSize,
    bool aimDownSights,
    float deltaTime) noexcept
{
    if (!finite(targetWorldPosition) ||
        !finite(shootingOrigin) ||
        !finite(worldSize) ||
        worldSize.x <= 0.0F || worldSize.y <= 0.0F ||
        !std::isfinite(deltaTime) || deltaTime < 0.0F)
    {
        return;
    }

    targetWorldPosition_ = Vec2{
        std::clamp(targetWorldPosition.x, 0.0F, worldSize.x),
        std::clamp(targetWorldPosition.y, 0.0F, worldSize.y)};
    shootingOrigin_ = shootingOrigin;
    worldSize_ = worldSize;

    if (!initialized_)
    {
        currentWorldPosition_ = targetWorldPosition_;
        lastDirection_ = normalizedOr(
            Vec2{
                currentWorldPosition_.x - shootingOrigin_.x,
                currentWorldPosition_.y - shootingOrigin_.y},
            lastDirection_);
        initialized_ = true;
    }

    float remaining = deltaTime;
    while (remaining > 0.0F)
    {
        const float step = std::min(remaining, kMaximumAimStep);
        advanceStep(step);
        remaining -= step;
    }

    if (deltaTime > 0.0F)
    {
        const float progressStep =
            deltaTime / config_.aimDownSightsDurationSeconds;
        aimDownSightsProgress_ = std::clamp(
            aimDownSightsProgress_ +
                (aimDownSights ? progressStep : -progressStep),
            0.0F,
            1.0F);
    }
}

void WeaponAimState::advanceStep(float deltaTime) noexcept
{
    const Vec2 toTarget{
        targetWorldPosition_.x - currentWorldPosition_.x,
        targetWorldPosition_.y - currentWorldPosition_.y};
    const float distanceToTarget = length(toTarget);
    Vec2 desiredControlVelocity{};
    if (std::isfinite(distanceToTarget) && distanceToTarget > 0.0001F)
    {
        const float arrivalSpeed = std::sqrt(
            2.0F * config_.controlAcceleration * distanceToTarget);
        const float desiredSpeed = std::min(
            config_.maximumReticleSpeed,
            arrivalSpeed);
        desiredControlVelocity = Vec2{
            toTarget.x / distanceToTarget * desiredSpeed,
            toTarget.y / distanceToTarget * desiredSpeed};
    }

    controlVelocity_ = moveTowards(
        controlVelocity_,
        desiredControlVelocity,
        config_.controlAcceleration * deltaTime);
    recoilVelocity_ = moveTowards(
        recoilVelocity_,
        Vec2{},
        config_.recoilDeceleration * deltaTime);

    currentWorldPosition_.x +=
        (controlVelocity_.x + recoilVelocity_.x) * deltaTime;
    currentWorldPosition_.y +=
        (controlVelocity_.y + recoilVelocity_.y) * deltaTime;
    currentWorldPosition_.x = std::clamp(
        currentWorldPosition_.x, 0.0F, worldSize_.x);
    currentWorldPosition_.y = std::clamp(
        currentWorldPosition_.y, 0.0F, worldSize_.y);

    lastDirection_ = normalizedOr(
        Vec2{
            currentWorldPosition_.x - shootingOrigin_.x,
            currentWorldPosition_.y - shootingOrigin_.y},
        lastDirection_);
}

void WeaponAimState::applyShotRecoil(Vec2 shootingOrigin) noexcept
{
    if (!initialized_ || !finite(shootingOrigin))
    {
        return;
    }
    const Vec2 radial = normalizedOr(
        Vec2{
            currentWorldPosition_.x - shootingOrigin.x,
            currentWorldPosition_.y - shootingOrigin.y},
        lastDirection_);
    const Vec2 lateral{-radial.y, radial.x};
    const float lateralSpeed =
        config_.recoilInitialSpeed *
        config_.recoilLateralRatio *
        nextSignedUnit();

    // Assignment is intentional: a new shot refreshes the recoil motion
    // instead of stacking an unbounded impulse.
    recoilVelocity_ = Vec2{
        radial.x * config_.recoilInitialSpeed +
            lateral.x * lateralSpeed,
        radial.y * config_.recoilInitialSpeed +
            lateral.y * lateralSpeed};
}

void WeaponAimState::reconfigure(WeaponAimConfig config)
{
    validateConfig(config);
    config_ = config;
    const float controlSpeed = length(controlVelocity_);
    if (controlSpeed > config_.maximumReticleSpeed)
    {
        const Vec2 direction = normalizedOr(controlVelocity_, Vec2{});
        controlVelocity_ = Vec2{
            direction.x * config_.maximumReticleSpeed,
            direction.y * config_.maximumReticleSpeed};
    }
}

Vec2 WeaponAimState::actualWorldPosition() const noexcept
{
    return currentWorldPosition_;
}

Vec2 WeaponAimState::targetWorldPosition() const noexcept
{
    return targetWorldPosition_;
}

Vec2 WeaponAimState::actualDirection() const noexcept
{
    return lastDirection_;
}

Vec2 WeaponAimState::controlVelocity() const noexcept
{
    return controlVelocity_;
}

Vec2 WeaponAimState::recoilVelocity() const noexcept
{
    return recoilVelocity_;
}

float WeaponAimState::aimDistance() const noexcept
{
    return length(Vec2{
        currentWorldPosition_.x - shootingOrigin_.x,
        currentWorldPosition_.y - shootingOrigin_.y});
}

float WeaponAimState::aimDownSightsProgress() const noexcept
{
    return aimDownSightsProgress_;
}

bool WeaponAimState::beyondMaximumRange() const noexcept
{
    return aimDistance() > config_.maximumRange;
}

float WeaponAimState::rangeSpreadFactor() const noexcept
{
    const float distance = aimDistance();
    if (distance <= config_.effectiveRange)
    {
        return 0.0F;
    }
    const float span = config_.maximumRange - config_.effectiveRange;
    if (span <= 0.0F)
    {
        return 1.0F;
    }
    return std::clamp(
        (distance - config_.effectiveRange) / span,
        0.0F,
        1.0F);
}

float WeaponAimState::damageMultiplier() const noexcept
{
    return beyondMaximumRange() ? 0.25F : 1.0F;
}

bool WeaponAimState::recoilActive() const noexcept
{
    return length(recoilVelocity_) > kVelocityEpsilon;
}

float WeaponAimState::nextSignedUnit() noexcept
{
    constexpr double denominator{4294967295.0};
    const double unit =
        static_cast<double>(recoilRandom_.next()) / denominator;
    return static_cast<float>(unit * 2.0 - 1.0);
}
