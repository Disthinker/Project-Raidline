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

    float moveTowards(
        float current,
        float target,
        float maximumDelta) noexcept
    {
        const float delta = target - current;
        if (std::abs(delta) <= maximumDelta)
        {
            return target;
        }
        return current + std::copysign(maximumDelta, delta);
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
    updateContextualEnvelope(WeaponFireContext{});
    refreshSpread();
}

void WeaponFireState::reconfigure(WeaponFireConfig config)
{
    validateConfig(config);
    config_ = config;
    movementBloomFraction_ = std::clamp(
        movementBloomFraction_, 0.0F, 1.0F);
    reticleMotionBloomFraction_ = std::clamp(
        reticleMotionBloomFraction_, 0.0F, 1.0F);
    shotBloomFraction_ = std::clamp(shotBloomFraction_, 0.0F, 1.0F);
    WeaponFireContext previousContext;
    previousContext.aimDownSightsProgress = lastAimDownSightsProgress_;
    previousContext.distanceSpreadFactor = lastDistanceSpreadFactor_;
    previousContext.overEffectiveRangeFactor =
        lastOverEffectiveRangeFactor_;
    updateContextualEnvelope(previousContext);
    refreshSpread();
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

    updateContextualEnvelope(context);
    updateMovementAndMotionBloom(deltaTime, context);
    recoverShotBloom(triggerPressed, deltaTime);

    if (context.forceMaximumSpread)
    {
        shotBloomFraction_ = 1.0F;
        recoveryDelayRemaining_ = config_.recoveryDelay;
    }
    refreshSpread();

    if (deltaTime > 0.0F)
    {
        cooldownRemaining_ = std::max(0.0F, cooldownRemaining_ - deltaTime);
    }

    if (!triggerPressed || cooldownRemaining_ > 0.0F ||
        !validDirection(baseAimDirection))
    {
        return std::nullopt;
    }

    const float offset = nextSignedUnit() * spreadDegrees_;
    cooldownRemaining_ = config_.shotInterval;
    recoveryDelayRemaining_ = config_.recoveryDelay;
    const float baseEnvelope = baseSpreadEnvelopeDegrees();
    if (baseEnvelope > 0.0001F)
    {
        shotBloomFraction_ = std::min(
            1.0F,
            shotBloomFraction_ +
                config_.spreadPerShotDegrees / baseEnvelope);
    }
    refreshSpread();
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

float WeaponFireState::spreadPresentationFraction() const noexcept
{
    if (contextualMaximumSpreadDegrees_ -
            contextualMinimumSpreadDegrees_ <= 0.0001F)
    {
        return 0.0F;
    }
    return combinedBloomFraction_;
}

float WeaponFireState::cooldownRemaining() const noexcept
{
    return cooldownRemaining_;
}

void WeaponFireState::updateContextualEnvelope(
    WeaponFireContext context) noexcept
{
    lastAimDownSightsProgress_ = std::clamp(
        context.aimDownSightsProgress, 0.0F, 1.0F);
    lastDistanceSpreadFactor_ = std::clamp(
        context.distanceSpreadFactor, 0.0F, 1.0F);
    lastOverEffectiveRangeFactor_ = std::clamp(
        context.overEffectiveRangeFactor, 0.0F, 1.0F);
    const float adsAccuracy = std::lerp(
        1.0F,
        config_.aimDownSightsAccuracyMultiplier,
        lastAimDownSightsProgress_);
    const float adsStability = std::lerp(
        1.0F,
        config_.aimDownSightsStabilityMultiplier,
        lastAimDownSightsProgress_);
    const float distanceScale = std::lerp(
        config_.nearDistanceSpreadScale,
        1.0F,
        lastDistanceSpreadFactor_);
    const float overEffectiveScale = std::lerp(
        1.0F,
        config_.overEffectiveRangeSpreadMultiplier,
        lastOverEffectiveRangeFactor_);
    contextualMinimumSpreadDegrees_ =
        config_.minimumSpreadDegrees * adsAccuracy *
        distanceScale * overEffectiveScale;
    contextualMaximumSpreadDegrees_ = std::max(
        contextualMinimumSpreadDegrees_,
        config_.maximumSpreadDegrees * adsStability *
            distanceScale * overEffectiveScale);
}

void WeaponFireState::updateMovementAndMotionBloom(
    float deltaTime,
    WeaponFireContext context) noexcept
{
    constexpr float kActivationFloorFraction{0.35F};
    constexpr float kMovementAttackFractionPerSecond{8.0F};
    const float recoveryRate = bloomRecoveryFractionPerSecond();
    const float movementTarget = context.moving
        ? config_.movingSpreadFraction
        : 0.0F;
    if (movementTarget > movementBloomFraction_)
    {
        movementBloomFraction_ = std::max(
            movementBloomFraction_,
            movementTarget * kActivationFloorFraction);
    }
    const float movementRate = movementTarget > movementBloomFraction_
        ? kMovementAttackFractionPerSecond
        : recoveryRate;
    movementBloomFraction_ = moveTowards(
        movementBloomFraction_,
        movementTarget,
        movementRate * deltaTime);

    float motionTarget{};
    if (config_.reticleMotionSpreadDegreesPerSecond > 0.0F)
    {
        motionTarget = smoothstep(
            (context.reticleControlSpeed -
             config_.reticleMotionSoftThreshold) /
            (config_.reticleMotionFullSpeed -
             config_.reticleMotionSoftThreshold));
    }
    if (motionTarget > reticleMotionBloomFraction_)
    {
        reticleMotionBloomFraction_ = std::max(
            reticleMotionBloomFraction_,
            motionTarget * kActivationFloorFraction);
    }
    const float baseEnvelope = baseSpreadEnvelopeDegrees();
    const float motionAttackRate = baseEnvelope > 0.0001F
        ? config_.reticleMotionSpreadDegreesPerSecond / baseEnvelope
        : 0.0F;
    const float motionRate = motionTarget > reticleMotionBloomFraction_
        ? motionAttackRate
        : recoveryRate;
    reticleMotionBloomFraction_ = moveTowards(
        reticleMotionBloomFraction_,
        motionTarget,
        motionRate * deltaTime);
}

void WeaponFireState::recoverShotBloom(
    bool triggerPressed,
    float deltaTime) noexcept
{
    if (triggerPressed)
    {
        recoveryDelayRemaining_ = config_.recoveryDelay;
        return;
    }

    float recoveryTime = deltaTime;
    if (recoveryDelayRemaining_ > 0.0F)
    {
        const float consumed = std::min(
            recoveryDelayRemaining_, recoveryTime);
        recoveryDelayRemaining_ -= consumed;
        recoveryTime -= consumed;
    }
    if (recoveryTime > 0.0F)
    {
        shotBloomFraction_ = moveTowards(
            shotBloomFraction_,
            0.0F,
            bloomRecoveryFractionPerSecond() * recoveryTime);
    }
    if (shotBloomFraction_ <= 0.0001F)
    {
        shotBloomFraction_ = 0.0F;
        burstShotCount_ = 0U;
    }
}

void WeaponFireState::refreshSpread() noexcept
{
    const float remainingPrecision =
        (1.0F - std::clamp(movementBloomFraction_, 0.0F, 1.0F)) *
        (1.0F - std::clamp(reticleMotionBloomFraction_, 0.0F, 1.0F)) *
        (1.0F - std::clamp(shotBloomFraction_, 0.0F, 1.0F));
    combinedBloomFraction_ = std::clamp(
        1.0F - remainingPrecision, 0.0F, 1.0F);
    spreadDegrees_ = std::lerp(
        contextualMinimumSpreadDegrees_,
        contextualMaximumSpreadDegrees_,
        combinedBloomFraction_);
}

float WeaponFireState::baseSpreadEnvelopeDegrees() const noexcept
{
    return std::max(
        0.0F,
        config_.maximumSpreadDegrees - config_.minimumSpreadDegrees);
}

float WeaponFireState::bloomRecoveryFractionPerSecond() const noexcept
{
    const float envelope = baseSpreadEnvelopeDegrees();
    if (envelope <= 0.0001F)
    {
        return 0.0F;
    }
    return config_.spreadRecoveryDegreesPerSecond / envelope;
}

float WeaponFireState::nextSignedUnit() noexcept
{
    constexpr double denominator{4294967295.0};
    const double unit =
        static_cast<double>(random_.next()) / denominator;
    return static_cast<float>(unit * 2.0 - 1.0);
}
