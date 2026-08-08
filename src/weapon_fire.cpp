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

    bool validDirection(Vec2 direction)
    {
        const float lengthSquared =
            direction.x * direction.x +
            direction.y * direction.y;

        return std::isfinite(lengthSquared) &&
               lengthSquared > 0.0F;
    }

    Vec2 normalized(Vec2 direction)
    {
        const float length = std::sqrt(
            direction.x * direction.x +
            direction.y * direction.y);

        return Vec2{
            direction.x / length,
            direction.y / length};
    }

    Vec2 rotated(Vec2 direction, float degrees)
    {
        const float radians = degrees * kPi / 180.0F;
        const float cosine = std::cos(radians);
        const float sine = std::sin(radians);

        return normalized(
            Vec2{
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
      randomState_{config.spreadSeed == 0U
                       ? 0x6D2B79F5U
                       : config.spreadSeed}
{
    if (!std::isfinite(config_.shotInterval) ||
        config_.shotInterval <= 0.0F ||
        !finiteNonNegative(config_.spreadPerShotDegrees) ||
        !finiteNonNegative(config_.maximumSpreadDegrees) ||
        config_.spreadPerShotDegrees > config_.maximumSpreadDegrees ||
        !finiteNonNegative(config_.recoveryDelay) ||
        !finiteNonNegative(config_.spreadRecoveryDegreesPerSecond) ||
        !finiteNonNegative(config_.visualRecoilPerShot) ||
        !finiteNonNegative(config_.maximumVisualRecoil) ||
        config_.visualRecoilPerShot > config_.maximumVisualRecoil ||
        !finiteNonNegative(config_.visualRecoilRecoveryPerSecond))
    {
        throw std::invalid_argument{
            "WeaponFireConfig values are inconsistent"};
    }
}

std::optional<ShotSpec> WeaponFireState::update(
    bool triggerPressed,
    Vec2 baseAimDirection,
    float deltaTime)
{
    if (!std::isfinite(deltaTime) || deltaTime < 0.0F)
    {
        return std::nullopt;
    }

    if (deltaTime > 0.0F)
    {
        cooldownRemaining_ = std::max(
            0.0F,
            cooldownRemaining_ - deltaTime);

        if (triggerPressed)
        {
            recoveryDelayRemaining_ = config_.recoveryDelay;
        }
        else
        {
            recover(deltaTime);
        }
    }

    if (!triggerPressed ||
        cooldownRemaining_ > 0.0F ||
        !validDirection(baseAimDirection))
    {
        return std::nullopt;
    }

    const Vec2 aimDirection = normalized(baseAimDirection);
    const float offset = burstShotCount_ == 0U
                             ? 0.0F
                             : nextSignedUnit() * spreadDegrees_;

    cooldownRemaining_ = config_.shotInterval;
    recoveryDelayRemaining_ = config_.recoveryDelay;
    spreadDegrees_ = std::min(
        config_.maximumSpreadDegrees,
        spreadDegrees_ + config_.spreadPerShotDegrees);
    visualRecoilPixels_ = std::min(
        config_.maximumVisualRecoil,
        visualRecoilPixels_ + config_.visualRecoilPerShot);
    ++burstShotCount_;

    return ShotSpec{
        rotated(aimDirection, offset),
        offset};
}

float WeaponFireState::spreadDegrees() const noexcept
{
    return spreadDegrees_;
}

float WeaponFireState::visualRecoilPixels() const noexcept
{
    return visualRecoilPixels_;
}

float WeaponFireState::cooldownRemaining() const noexcept
{
    return cooldownRemaining_;
}

void WeaponFireState::recover(float deltaTime) noexcept
{
    float recoveryTime = deltaTime;

    if (recoveryDelayRemaining_ > 0.0F)
    {
        const float consumed = std::min(
            recoveryDelayRemaining_,
            recoveryTime);
        recoveryDelayRemaining_ -= consumed;
        recoveryTime -= consumed;
    }

    if (recoveryTime <= 0.0F)
    {
        return;
    }

    spreadDegrees_ = std::max(
        0.0F,
        spreadDegrees_ -
            config_.spreadRecoveryDegreesPerSecond * recoveryTime);
    visualRecoilPixels_ = std::max(
        0.0F,
        visualRecoilPixels_ -
            config_.visualRecoilRecoveryPerSecond * recoveryTime);

    if (spreadDegrees_ == 0.0F &&
        visualRecoilPixels_ == 0.0F)
    {
        burstShotCount_ = 0U;
    }
}

float WeaponFireState::nextSignedUnit() noexcept
{
    randomState_ ^= randomState_ << 13U;
    randomState_ ^= randomState_ >> 17U;
    randomState_ ^= randomState_ << 5U;

    constexpr float kMaximum24BitValue{16777215.0F};
    const float unit = static_cast<float>(
                           randomState_ & 0x00FFFFFFU) /
                       kMaximum24BitValue;

    return unit * 2.0F - 1.0F;
}
