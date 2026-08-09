#include "combat_feedback.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace
{
    bool isFinitePositive(float value) noexcept
    {
        return std::isfinite(value) && value > 0.0F;
    }

    bool isValidStrength(float value) noexcept
    {
        return isFinitePositive(value) && value <= 1.0F;
    }

    float decayTimer(float remaining, float deltaTime) noexcept
    {
        return std::max(0.0F, remaining - deltaTime);
    }

    float normalizedIntensity(float remaining, float duration) noexcept
    {
        return std::clamp(remaining / duration, 0.0F, 1.0F);
    }
}

CombatFeedbackState::CombatFeedbackState()
    : CombatFeedbackState{CombatFeedbackConfig{}}
{
}

CombatFeedbackState::CombatFeedbackState(
    CombatFeedbackConfig config)
    : config_{config}
{
    if (!isFinitePositive(config_.muzzleFlashDuration) ||
        !isFinitePositive(config_.hitConfirmDuration) ||
        !isFinitePositive(config_.playerDamagePulseDuration) ||
        !isValidStrength(config_.scratchPulseStrength) ||
        !isValidStrength(config_.bitePulseStrength) ||
        config_.scratchPulseStrength > config_.bitePulseStrength)
    {
        throw std::invalid_argument{
            "CombatFeedbackConfig contains invalid values"};
    }
}

void CombatFeedbackState::update(float deltaTime) noexcept
{
    if (!std::isfinite(deltaTime) || deltaTime <= 0.0F)
    {
        return;
    }

    muzzleFlashRemaining_ = decayTimer(
        muzzleFlashRemaining_,
        deltaTime);
    hitConfirmRemaining_ = decayTimer(
        hitConfirmRemaining_,
        deltaTime);
    playerDamagePulseRemaining_ = decayTimer(
        playerDamagePulseRemaining_,
        deltaTime);

    if (muzzleFlashRemaining_ <= 0.0F)
    {
        muzzleOrigin_ = {};
        muzzleDirection_ = {};
    }
    if (playerDamagePulseRemaining_ <= 0.0F)
    {
        playerDamagePulseStrength_ = 0.0F;
    }
}

bool CombatFeedbackState::recordShot(
    Vec2 origin,
    Vec2 direction) noexcept
{
    if (!std::isfinite(origin.x) ||
        !std::isfinite(origin.y) ||
        !std::isfinite(direction.x) ||
        !std::isfinite(direction.y))
    {
        return false;
    }

    const float lengthSquared =
        direction.x * direction.x +
        direction.y * direction.y;
    if (!std::isfinite(lengthSquared) ||
        lengthSquared <= 0.0F)
    {
        return false;
    }

    const float length = std::sqrt(lengthSquared);
    muzzleOrigin_ = origin;
    muzzleDirection_ = {
        direction.x / length,
        direction.y / length};
    muzzleFlashRemaining_ = config_.muzzleFlashDuration;
    return true;
}

void CombatFeedbackState::recordEnemyHit() noexcept
{
    hitConfirmRemaining_ = config_.hitConfirmDuration;
}

bool CombatFeedbackState::recordPlayerHit(
    EnemyAttackType type) noexcept
{
    float strength{};
    switch (type)
    {
    case EnemyAttackType::Scratch:
        strength = config_.scratchPulseStrength;
        break;
    case EnemyAttackType::Bite:
        strength = config_.bitePulseStrength;
        break;
    case EnemyAttackType::Grab:
        return false;
    default:
        return false;
    }

    playerDamagePulseStrength_ = std::max(
        playerDamagePulseStrength_,
        strength);
    playerDamagePulseRemaining_ = std::max(
        playerDamagePulseRemaining_,
        config_.playerDamagePulseDuration);
    return true;
}

void CombatFeedbackState::reset() noexcept
{
    muzzleFlashRemaining_ = 0.0F;
    hitConfirmRemaining_ = 0.0F;
    playerDamagePulseRemaining_ = 0.0F;
    playerDamagePulseStrength_ = 0.0F;
    muzzleOrigin_ = {};
    muzzleDirection_ = {};
}

float CombatFeedbackState::muzzleFlashIntensity() const noexcept
{
    return normalizedIntensity(
        muzzleFlashRemaining_,
        config_.muzzleFlashDuration);
}

float CombatFeedbackState::hitConfirmIntensity() const noexcept
{
    return normalizedIntensity(
        hitConfirmRemaining_,
        config_.hitConfirmDuration);
}

float CombatFeedbackState::playerDamagePulseIntensity() const noexcept
{
    return normalizedIntensity(
               playerDamagePulseRemaining_,
               config_.playerDamagePulseDuration) *
           playerDamagePulseStrength_;
}

Vec2 CombatFeedbackState::muzzleOrigin() const noexcept
{
    return muzzleOrigin_;
}

Vec2 CombatFeedbackState::muzzleDirection() const noexcept
{
    return muzzleDirection_;
}
