#include "weapon_aim.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace
{
    constexpr float kMaximumAimStep{1.0F / 120.0F};
    constexpr float kVelocityEpsilon{0.01F};
    constexpr float kMaximumRecoilDeflectionRadians{
        1.0471975512F}; // 60 degrees at a lateral ratio of 1.0.

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
            !finitePositive(config.recoilBendDurationSeconds) ||
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
    float deltaTime,
    std::optional<Vec2> inputMotionDelta) noexcept
{
    if (!finite(targetWorldPosition) ||
        !finite(shootingOrigin) ||
        !finite(worldSize) ||
        worldSize.x <= 0.0F || worldSize.y <= 0.0F ||
        !std::isfinite(deltaTime) || deltaTime < 0.0F)
    {
        return;
    }

    const Vec2 clampedInputPosition{
        std::clamp(targetWorldPosition.x, 0.0F, worldSize.x),
        std::clamp(targetWorldPosition.y, 0.0F, worldSize.y)};
    shootingOrigin_ = shootingOrigin;
    worldSize_ = worldSize;

    if (!initialized_)
    {
        inputWorldPosition_ = clampedInputPosition;
        targetWorldPosition_ = clampedInputPosition;
        currentWorldPosition_ = targetWorldPosition_;
        lastDirection_ = normalizedOr(
            Vec2{
                currentWorldPosition_.x - shootingOrigin_.x,
                currentWorldPosition_.y - shootingOrigin_.y},
            lastDirection_);
        initialized_ = true;
    }
    else if (inputMotionDelta.has_value() && finite(*inputMotionDelta))
    {
        // Relative mouse input is consumed as motion, not as a bounded OS
        // cursor position. This keeps aiming continuous while the pointer is
        // captured at a window edge and avoids a dead zone when reversing.
        targetWorldPosition_.x = std::clamp(
            targetWorldPosition_.x + inputMotionDelta->x,
            0.0F,
            worldSize_.x);
        targetWorldPosition_.y = std::clamp(
            targetWorldPosition_.y + inputMotionDelta->y,
            0.0F,
            worldSize_.y);
        inputWorldPosition_ = clampedInputPosition;
    }
    else
    {
        // Mouse motion moves the reticle destination by the same delta. It is
        // deliberately not treated as a permanent absolute return point:
        // recoil may displace the reticle, and recovering that displacement
        // requires an opposing mouse movement from the player.
        targetWorldPosition_.x = std::clamp(
            targetWorldPosition_.x +
                clampedInputPosition.x - inputWorldPosition_.x,
            0.0F,
            worldSize_.x);
        targetWorldPosition_.y = std::clamp(
            targetWorldPosition_.y +
                clampedInputPosition.y - inputWorldPosition_.y,
            0.0F,
            worldSize_.y);
        inputWorldPosition_ = clampedInputPosition;
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
    const float recoilSpeed = length(recoilVelocity_);
    if (recoilSpeed > kVelocityEpsilon)
    {
        Vec2 recoilDirection = normalizedOr(
            recoilVelocity_,
            recoilTargetDirection_);
        if (recoilBendRemainingSeconds_ > 0.0F)
        {
            // The shot starts with an immediate outward impulse, then bends
            // continuously toward its sampled lateral direction. Blending by
            // the remaining duration reaches the exact target direction
            // without turning a large lateral value into a one-frame jump.
            const float blend = std::clamp(
                deltaTime / recoilBendRemainingSeconds_,
                0.0F,
                1.0F);
            recoilDirection = normalizedOr(
                Vec2{
                    recoilDirection.x +
                        (recoilTargetDirection_.x - recoilDirection.x) * blend,
                    recoilDirection.y +
                        (recoilTargetDirection_.y - recoilDirection.y) * blend},
                recoilTargetDirection_);
            recoilBendRemainingSeconds_ = std::max(
                0.0F,
                recoilBendRemainingSeconds_ - deltaTime);
        }

        const float nextRecoilSpeed = std::max(
            0.0F,
            recoilSpeed - config_.recoilDeceleration * deltaTime);
        recoilVelocity_ = Vec2{
            recoilDirection.x * nextRecoilSpeed,
            recoilDirection.y * nextRecoilSpeed};
    }
    else
    {
        recoilVelocity_ = Vec2{};
        recoilBendRemainingSeconds_ = 0.0F;
    }

    const Vec2 recoilDisplacement{
        recoilVelocity_.x * deltaTime,
        recoilVelocity_.y * deltaTime};

    // Recoil moves both the reticle and its control destination. Otherwise a
    // stationary absolute mouse position would become an automatic recovery
    // spring after every shot.
    targetWorldPosition_.x = std::clamp(
        targetWorldPosition_.x + recoilDisplacement.x,
        0.0F,
        worldSize_.x);
    targetWorldPosition_.y = std::clamp(
        targetWorldPosition_.y + recoilDisplacement.y,
        0.0F,
        worldSize_.y);

    currentWorldPosition_.x +=
        controlVelocity_.x * deltaTime + recoilDisplacement.x;
    currentWorldPosition_.y +=
        controlVelocity_.y * deltaTime + recoilDisplacement.y;
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
    const float randomLateral = nextSignedUnit();
    const float lateralStrength = std::copysign(
        0.35F + 0.65F * std::abs(randomLateral),
        randomLateral);
    const float deflection =
        lateralStrength * config_.recoilLateralRatio *
        kMaximumRecoilDeflectionRadians;
    const Vec2 recoilDirection{
        radial.x * std::cos(deflection) + lateral.x * std::sin(deflection),
        radial.y * std::cos(deflection) + lateral.y * std::sin(deflection)};

    // A new shot refreshes one bounded motion instead of stacking impulses.
    // Its initial velocity is always radial and therefore immediately
    // readable; the sampled lateral component appears as a continuous curve.
    recoilVelocity_ = Vec2{
        radial.x * config_.recoilInitialSpeed,
        radial.y * config_.recoilInitialSpeed};
    recoilTargetDirection_ = recoilDirection;
    recoilBendRemainingSeconds_ = config_.recoilBendDurationSeconds;
}

void WeaponAimState::reconfigure(WeaponAimConfig config)
{
    validateConfig(config);
    config_ = config;
    if (recoilBendRemainingSeconds_ > config_.recoilBendDurationSeconds)
    {
        recoilBendRemainingSeconds_ = config_.recoilBendDurationSeconds;
    }
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

Vec2 WeaponAimState::recoilPresentationVelocity() const noexcept
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
