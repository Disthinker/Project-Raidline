#include "enemy_ai.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace
{
    bool isFinitePositive(float value) noexcept
    {
        return std::isfinite(value) && value > 0.0F;
    }

    void validateConfig(const EnemyAiConfig &config)
    {
        if (!isFinitePositive(config.normalMoveSpeed) ||
            !isFinitePositive(config.attackMoveSpeed) ||
            !isFinitePositive(config.stopDistance) ||
            !isFinitePositive(config.scratchAttackDistance) ||
            !isFinitePositive(config.specialChargeMinDistance) ||
            !isFinitePositive(config.specialChargeMaxDistance) ||
            !isFinitePositive(config.specialChargeHoldDuration) ||
            !isFinitePositive(config.grabCooldown) ||
            !isFinitePositive(config.scratchCooldown) ||
            config.normalMoveSpeed >= config.attackMoveSpeed ||
            config.stopDistance >= config.scratchAttackDistance ||
            config.scratchAttackDistance >=
                config.specialChargeMinDistance ||
            config.specialChargeMinDistance >=
                config.specialChargeMaxDistance)
        {
            throw std::invalid_argument{
                "EnemyAiConfig contains invalid thresholds or cooldowns"};
        }
    }
}

EnemyAiState::EnemyAiState()
    : EnemyAiState{EnemyAiConfig{}}
{
}

EnemyAiState::EnemyAiState(EnemyAiConfig config)
    : config_{config}
{
    validateConfig(config_);
}

EnemyAiDecision EnemyAiState::update(
    Vec2 targetOffset,
    bool canStartAttack,
    float deltaTime) noexcept
{
    advanceCooldowns(deltaTime);

    const float distanceSquared =
        targetOffset.x * targetOffset.x +
        targetOffset.y * targetOffset.y;

    if (!std::isfinite(distanceSquared) ||
        distanceSquared <= 0.0F ||
        !canStartAttack)
    {
        return EnemyAiDecision{};
    }

    const float distance = std::sqrt(distanceSquared);
    const Vec2 direction{
        targetOffset.x / distance,
        targetOffset.y / distance};

    const bool inSpecialChargeBand =
        distance >= config_.specialChargeMinDistance &&
        distance <= config_.specialChargeMaxDistance;
    if (specialChargeArmed_ && inSpecialChargeBand)
    {
        if (std::isfinite(deltaTime) &&
            deltaTime > 0.0F)
        {
            specialChargeHoldTime_ = std::min(
                config_.specialChargeHoldDuration,
                specialChargeHoldTime_ + deltaTime);
        }

        if (specialChargeHoldTime_ >=
                config_.specialChargeHoldDuration &&
            attackReady(EnemyAttackType::Grab))
        {
            return EnemyAiDecision{
                Vec2{},
                EnemyAttackType::Grab};
        }
    }
    else
    {
        specialChargeHoldTime_ = 0.0F;
    }

    if (distance <= config_.scratchAttackDistance)
    {
        if (attackReady(EnemyAttackType::Scratch))
        {
            return EnemyAiDecision{
                Vec2{},
                EnemyAttackType::Scratch};
        }

        if (distance > config_.stopDistance)
        {
            return EnemyAiDecision{direction, std::nullopt};
        }

        return EnemyAiDecision{};
    }

    return EnemyAiDecision{direction, std::nullopt};
}

void EnemyAiState::recordAttackStarted(
    EnemyAttackType type) noexcept
{
    switch (type)
    {
    case EnemyAttackType::Grab:
        grabCooldownRemaining_ = config_.grabCooldown;
        specialChargeArmed_ = false;
        specialChargeHoldTime_ = 0.0F;
        break;
    case EnemyAttackType::Scratch:
        scratchCooldownRemaining_ = config_.scratchCooldown;
        specialChargeArmed_ = true;
        specialChargeHoldTime_ = 0.0F;
        break;
    case EnemyAttackType::Bite:
        break;
    }
}

void EnemyAiState::reset() noexcept
{
    grabCooldownRemaining_ = 0.0F;
    scratchCooldownRemaining_ = 0.0F;
    specialChargeArmed_ = false;
    specialChargeHoldTime_ = 0.0F;
}

const EnemyAiConfig &EnemyAiState::config() const noexcept
{
    return config_;
}

float EnemyAiState::cooldownRemaining(
    EnemyAttackType type) const noexcept
{
    switch (type)
    {
    case EnemyAttackType::Grab:
        return grabCooldownRemaining_;
    case EnemyAttackType::Scratch:
        return scratchCooldownRemaining_;
    case EnemyAttackType::Bite:
        return 0.0F;
    }

    return 0.0F;
}

bool EnemyAiState::specialChargeArmed() const noexcept
{
    return specialChargeArmed_;
}

float EnemyAiState::specialChargeHoldRemaining() const noexcept
{
    return std::max(
        0.0F,
        config_.specialChargeHoldDuration -
            specialChargeHoldTime_);
}

void EnemyAiState::advanceCooldowns(float deltaTime) noexcept
{
    if (!std::isfinite(deltaTime) ||
        deltaTime <= 0.0F)
    {
        return;
    }

    grabCooldownRemaining_ = std::max(
        0.0F,
        grabCooldownRemaining_ - deltaTime);
    scratchCooldownRemaining_ = std::max(
        0.0F,
        scratchCooldownRemaining_ - deltaTime);
}

bool EnemyAiState::attackReady(
    EnemyAttackType type) const noexcept
{
    return cooldownRemaining(type) <= 0.0F;
}
