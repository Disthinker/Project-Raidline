#include "weapon_fire.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace
{
    constexpr float kPi{3.14159265358979323846F};

    bool finiteNonNegative(float value)
    {
        return std::isfinite(value) && value >= 0.0F;
    }

    void validateConfig(const WeaponFireConfig &config)
    {
        if (!std::isfinite(config.shotInterval) ||
            config.shotInterval <= 0.0F ||
            !finiteNonNegative(config.minimumSpreadDegrees) ||
            !finiteNonNegative(config.maximumSpreadDegrees) ||
            config.minimumSpreadDegrees > config.maximumSpreadDegrees ||
            !finiteNonNegative(config.spreadPerShotDegrees) ||
            !finiteNonNegative(config.recoveryDelay) ||
            !finiteNonNegative(config.spreadRecoveryDegreesPerSecond) ||
            !std::isfinite(config.aimDownSightsAccuracyMultiplier) ||
            config.aimDownSightsAccuracyMultiplier <= 0.0F ||
            config.aimDownSightsAccuracyMultiplier > 1.0F ||
            !std::isfinite(config.aimDownSightsStabilityMultiplier) ||
            config.aimDownSightsStabilityMultiplier <= 0.0F ||
            config.aimDownSightsStabilityMultiplier > 1.0F ||
            !std::isfinite(config.movingSpreadFraction) ||
            config.movingSpreadFraction < 0.0F ||
            config.movingSpreadFraction > 1.0F ||
            !finiteNonNegative(config.reticleMotionSpreadDegreesPerSecond) ||
            !finiteNonNegative(config.reticleMotionSoftThreshold) ||
            !std::isfinite(config.reticleMotionFullSpeed) ||
            config.reticleMotionFullSpeed <= config.reticleMotionSoftThreshold ||
            !std::isfinite(config.nearDistanceSpreadScale) ||
            config.nearDistanceSpreadScale < 0.0F ||
            config.nearDistanceSpreadScale > 1.0F ||
            !std::isfinite(config.overEffectiveRangeSpreadMultiplier) ||
            config.overEffectiveRangeSpreadMultiplier < 1.0F)
        {
            throw std::invalid_argument{
                "WeaponFireConfig values are inconsistent"};
        }
    }

    bool validDirection(Vec2 direction)
    {
        const float lengthSquared =
            direction.x * direction.x + direction.y * direction.y;
        return std::isfinite(lengthSquared) && lengthSquared > 0.0F;
    }

    Vec2 normalized(Vec2 direction)
    {
        const float length = std::sqrt(
            direction.x * direction.x + direction.y * direction.y);
        return Vec2{direction.x / length, direction.y / length};
    }

    Vec2 rotated(Vec2 direction, float degrees)
    {
        const float radians = degrees * kPi / 180.0F;
        const float cosine = std::cos(radians);
        const float sine = std::sin(radians);
        return normalized(Vec2{
            direction.x * cosine - direction.y * sine,
            direction.x * sine + direction.y * cosine});
    }

    float smoothstep(float value) noexcept
    {
        const float clamped = std::clamp(value, 0.0F, 1.0F);
        return clamped * clamped * (3.0F - 2.0F * clamped);
    }
}

WeaponFireState::WeaponFireState()
    : WeaponFireState{WeaponFireConfig{}}
{
}

WeaponFireState::WeaponFireState(WeaponFireConfig config)
    : config_{config},
      spreadDegrees_{config.minimumSpreadDegrees},
      contextualMinimumSpreadDegrees_{config.minimumSpreadDegrees},
      contextualMaximumSpreadDegrees_{config.maximumSpreadDegrees},
      random_{
          config.spreadSeed == 0U ? 0x737072656164ULL : config.spreadSeed,
          0x776561706f6e2d73ULL}
{
    validateConfig(config_);
}

void WeaponFireState::reconfigure(WeaponFireConfig config)
{
    validateConfig(config);
    config_ = config;
    spreadDegrees_ = std::clamp(
        spreadDegrees_,
        0.0F,
        config_.maximumSpreadDegrees *
            config_.overEffectiveRangeSpreadMultiplier);
    contextualMinimumSpreadDegrees_ = std::clamp(
        contextualMinimumSpreadDegrees_,
        0.0F,
        spreadDegrees_);
    contextualMaximumSpreadDegrees_ = std::max(
        contextualMinimumSpreadDegrees_,
        std::min(
            contextualMaximumSpreadDegrees_,
            config_.maximumSpreadDegrees *
                config_.overEffectiveRangeSpreadMultiplier));
}

std::optional<ShotSpec> WeaponFireState::update(
    bool triggerPressed,
    Vec2 baseAimDirection,
    float deltaTime,
    WeaponFireContext context)
{
    if (!std::isfinite(deltaTime) || deltaTime < 0.0F ||
        !std::isfinite(context.aimDownSightsProgress) ||
        !std::isfinite(context.distanceSpreadFactor) ||
        !std::isfinite(context.overEffectiveRangeFactor) ||
        !finiteNonNegative(context.reticleControlSpeed))
    {
        return std::nullopt;
    }

    const float adsProgress = std::clamp(
        context.aimDownSightsProgress, 0.0F, 1.0F);
    const float distanceFactor = std::clamp(
        context.distanceSpreadFactor, 0.0F, 1.0F);
    const float overEffectiveFactor = std::clamp(
        context.overEffectiveRangeFactor, 0.0F, 1.0F);
    const float adsAccuracy = std::lerp(
        1.0F, config_.aimDownSightsAccuracyMultiplier, adsProgress);
    const float distanceScale = std::lerp(
        config_.nearDistanceSpreadScale,
        1.0F,
        distanceFactor);
    const float overEffectiveScale = std::lerp(
        1.0F,
        config_.overEffectiveRangeSpreadMultiplier,
        overEffectiveFactor);
    contextualMinimumSpreadDegrees_ =
        config_.minimumSpreadDegrees * adsAccuracy *
        distanceScale * overEffectiveScale;
    contextualMaximumSpreadDegrees_ = std::max(
        contextualMinimumSpreadDegrees_,
        config_.maximumSpreadDegrees * std::lerp(
            1.0F,
            config_.aimDownSightsStabilityMultiplier,
            adsProgress) *
            distanceScale * overEffectiveScale);
    // Distance is itself an accuracy pressure. At the effective-range edge
    // the resting reticle reaches the normal in-range maximum; beyond it the
    // separate over-effective multiplier continues the degradation.
    float targetSpread = std::lerp(
        contextualMinimumSpreadDegrees_,
        contextualMaximumSpreadDegrees_,
        distanceFactor);
    if (context.moving)
    {
        targetSpread +=
            (contextualMaximumSpreadDegrees_ - targetSpread) *
            config_.movingSpreadFraction;
    }
    targetSpread = std::clamp(
        targetSpread,
        contextualMinimumSpreadDegrees_,
        contextualMaximumSpreadDegrees_);

    spreadDegrees_ = std::clamp(
        spreadDegrees_,
        contextualMinimumSpreadDegrees_,
        contextualMaximumSpreadDegrees_);
    spreadDegrees_ = std::max(spreadDegrees_, targetSpread);

    const float reticleMotionFactor = smoothstep(
        (context.reticleControlSpeed - config_.reticleMotionSoftThreshold) /
        (config_.reticleMotionFullSpeed -
         config_.reticleMotionSoftThreshold));
    const bool motionExpandedSpread =
        deltaTime > 0.0F && reticleMotionFactor > 0.0F;
    if (motionExpandedSpread)
    {
        const float motionTarget = std::lerp(
            targetSpread,
            contextualMaximumSpreadDegrees_,
            reticleMotionFactor);
        // A short flick can occupy only one rendered frame. Give it an
        // immediate, bounded portion of the envelope, then let the configured
        // rate carry sustained motion toward the full maximum.
        const float readableMotionFloor = std::lerp(
            targetSpread,
            motionTarget,
            0.35F);
        spreadDegrees_ = std::max(
            spreadDegrees_,
            readableMotionFloor);
        spreadDegrees_ = std::min(
            motionTarget,
            spreadDegrees_ +
                config_.reticleMotionSpreadDegreesPerSecond *
                    reticleMotionFactor * deltaTime);
        recoveryDelayRemaining_ = std::max(
            recoveryDelayRemaining_,
            std::min(config_.recoveryDelay, 0.04F));
    }

    if (context.forceMaximumSpread)
    {
        spreadDegrees_ = contextualMaximumSpreadDegrees_;
        recoveryDelayRemaining_ = config_.recoveryDelay;
    }

    if (deltaTime > 0.0F)
    {
        cooldownRemaining_ = std::max(0.0F, cooldownRemaining_ - deltaTime);
        if (triggerPressed)
        {
            recoveryDelayRemaining_ = config_.recoveryDelay;
        }
        else if (!context.forceMaximumSpread && !motionExpandedSpread)
        {
            recover(deltaTime, targetSpread);
        }
    }

    if (!triggerPressed || cooldownRemaining_ > 0.0F ||
        !validDirection(baseAimDirection))
    {
        return std::nullopt;
    }

    const float offset = nextSignedUnit() * spreadDegrees_;
    cooldownRemaining_ = config_.shotInterval;
    recoveryDelayRemaining_ = config_.recoveryDelay;
    spreadDegrees_ = std::min(
        contextualMaximumSpreadDegrees_,
        spreadDegrees_ + config_.spreadPerShotDegrees);
    ++burstShotCount_;

    return ShotSpec{rotated(normalized(baseAimDirection), offset), offset};
}

float WeaponFireState::spreadDegrees() const noexcept
{
    return spreadDegrees_;
}

float WeaponFireState::contextualMinimumSpreadDegrees() const noexcept
{
    return contextualMinimumSpreadDegrees_;
}

float WeaponFireState::contextualMaximumSpreadDegrees() const noexcept
{
    return contextualMaximumSpreadDegrees_;
}

float WeaponFireState::cooldownRemaining() const noexcept
{
    return cooldownRemaining_;
}

void WeaponFireState::recover(float deltaTime, float targetSpread) noexcept
{
    float recoveryTime = deltaTime;
    if (recoveryDelayRemaining_ > 0.0F)
    {
        const float consumed = std::min(recoveryDelayRemaining_, recoveryTime);
        recoveryDelayRemaining_ -= consumed;
        recoveryTime -= consumed;
    }
    if (recoveryTime <= 0.0F)
    {
        return;
    }
    spreadDegrees_ = std::max(
        targetSpread,
        spreadDegrees_ - config_.spreadRecoveryDegreesPerSecond * recoveryTime);
    if (spreadDegrees_ <= targetSpread)
    {
        burstShotCount_ = 0U;
    }
}

float WeaponFireState::nextSignedUnit() noexcept
{
    constexpr double denominator{4294967295.0};
    const double unit =
        static_cast<double>(random_.next()) / denominator;
    return static_cast<float>(unit * 2.0 - 1.0);
}
