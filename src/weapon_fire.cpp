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
            config.movingSpreadFraction > 1.0F)
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
}

WeaponFireState::WeaponFireState()
    : WeaponFireState{WeaponFireConfig{}}
{
}

WeaponFireState::WeaponFireState(WeaponFireConfig config)
    : config_{config},
      spreadDegrees_{config.minimumSpreadDegrees},
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
        config_.minimumSpreadDegrees,
        config_.maximumSpreadDegrees);
}

std::optional<ShotSpec> WeaponFireState::update(
    bool triggerPressed,
    Vec2 baseAimDirection,
    float deltaTime,
    WeaponFireContext context)
{
    if (!std::isfinite(deltaTime) || deltaTime < 0.0F ||
        !std::isfinite(context.aimDownSightsProgress) ||
        !std::isfinite(context.rangeSpreadFactor))
    {
        return std::nullopt;
    }

    const float adsProgress = std::clamp(
        context.aimDownSightsProgress, 0.0F, 1.0F);
    const float rangeFactor = std::clamp(
        context.rangeSpreadFactor, 0.0F, 1.0F);
    const float adsAccuracy = std::lerp(
        1.0F, config_.aimDownSightsAccuracyMultiplier, adsProgress);
    const float effectiveMaximumSpread = std::max(
        config_.minimumSpreadDegrees * adsAccuracy,
        config_.maximumSpreadDegrees * std::lerp(
            1.0F,
            config_.aimDownSightsStabilityMultiplier,
            adsProgress));
    float targetSpread = config_.minimumSpreadDegrees * adsAccuracy;
    if (context.moving)
    {
        targetSpread +=
            (effectiveMaximumSpread - targetSpread) *
            config_.movingSpreadFraction;
    }
    targetSpread +=
        (effectiveMaximumSpread - targetSpread) * rangeFactor;
    targetSpread = std::clamp(
        targetSpread, 0.0F, effectiveMaximumSpread);

    if (context.forceMaximumSpread)
    {
        spreadDegrees_ = effectiveMaximumSpread;
        recoveryDelayRemaining_ = config_.recoveryDelay;
    }
    else
    {
        spreadDegrees_ = std::max(spreadDegrees_, targetSpread);
    }

    if (deltaTime > 0.0F)
    {
        cooldownRemaining_ = std::max(0.0F, cooldownRemaining_ - deltaTime);
        if (triggerPressed)
        {
            recoveryDelayRemaining_ = config_.recoveryDelay;
        }
        else if (!context.forceMaximumSpread)
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
        effectiveMaximumSpread,
        spreadDegrees_ + config_.spreadPerShotDegrees);
    ++burstShotCount_;

    return ShotSpec{rotated(normalized(baseAimDirection), offset), offset};
}

float WeaponFireState::spreadDegrees() const noexcept
{
    return spreadDegrees_;
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
