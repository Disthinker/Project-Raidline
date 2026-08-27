#include "enemy_ai.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
#include <stdexcept>

namespace
{
    constexpr float kDirectionEpsilon{0.00001F};

    bool isFinitePositive(float value) noexcept
    {
        return std::isfinite(value) && value > 0.0F;
    }

    bool isFinite(Vec2 value) noexcept
    {
        return std::isfinite(value.x) &&
               std::isfinite(value.y);
    }

    float lengthSquared(Vec2 value) noexcept
    {
        return value.x * value.x +
               value.y * value.y;
    }

    Vec2 normalizedOrZero(Vec2 value) noexcept
    {
        const float squaredLength = lengthSquared(value);
        if (!std::isfinite(squaredLength) ||
            squaredLength <= kDirectionEpsilon * kDirectionEpsilon)
        {
            return Vec2{};
        }

        const float length = std::sqrt(squaredLength);
        return Vec2{
            value.x / length,
            value.y / length};
    }

    float distanceBetween(
        Vec2 first,
        Vec2 second) noexcept
    {
        const Vec2 offset{
            second.x - first.x,
            second.y - first.y};
        const float squaredDistance = lengthSquared(offset);
        if (!std::isfinite(squaredDistance))
        {
            return std::numeric_limits<float>::infinity();
        }
        return std::sqrt(squaredDistance);
    }

    float wrappedAngle(float angle) noexcept
    {
        constexpr float twoPi =
            2.0F * std::numbers::pi_v<float>;
        while (angle > std::numbers::pi_v<float>)
        {
            angle -= twoPi;
        }
        while (angle < -std::numbers::pi_v<float>)
        {
            angle += twoPi;
        }
        return angle;
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
            !isFinitePositive(config.acquireTargetDistance) ||
            !isFinitePositive(config.loseTargetDistance) ||
            !isFinitePositive(config.searchMemoryDuration) ||
            !isFinitePositive(config.searchArrivalDistance) ||
            !isFinitePositive(config.maximumTurnRateRadians) ||
            !isFinitePositive(config.supportMinDistance) ||
            !isFinitePositive(config.supportMaxDistance) ||
            config.normalMoveSpeed >= config.attackMoveSpeed ||
            config.stopDistance >= config.scratchAttackDistance ||
            config.scratchAttackDistance >=
                config.specialChargeMinDistance ||
            config.specialChargeMinDistance >=
                config.specialChargeMaxDistance ||
            config.acquireTargetDistance >=
                config.loseTargetDistance ||
            config.searchArrivalDistance >=
                config.acquireTargetDistance ||
            config.scratchAttackDistance >=
                config.supportMinDistance ||
            config.supportMinDistance >=
                config.supportMaxDistance)
        {
            throw std::invalid_argument{
                "EnemyAiConfig contains invalid thresholds or cooldowns"};
        }
    }
}

const char *enemyAwarenessStateName(
    EnemyAwarenessState state) noexcept
{
    switch (state)
    {
    case EnemyAwarenessState::Unaware:
        return "Unaware";
    case EnemyAwarenessState::Alerted:
        return "Alerted";
    case EnemyAwarenessState::Searching:
        return "Searching";
    }

    return "Unknown";
}

const char *enemyTacticalRoleName(
    EnemyTacticalRole role) noexcept
{
    switch (role)
    {
    case EnemyTacticalRole::Engage:
        return "Engage";
    case EnemyTacticalRole::Pressure:
        return "Pressure";
    case EnemyTacticalRole::Support:
        return "Support";
    }

    return "Unknown";
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
    const EnemyAiInput &input) noexcept
{
    if (!isFinite(input.selfPosition) ||
        !isFinite(input.targetPosition) ||
        !isFinite(input.tactical.separationDirection) ||
        !std::isfinite(input.tactical.supportSide) ||
        (input.navigationTarget.has_value() &&
         !isFinite(*input.navigationTarget)))
    {
        return EnemyAiDecision{};
    }

    advanceCooldowns(input.deltaTime);
    updatePerception(
        input.selfPosition,
        input.targetPosition,
        input.targetVisible,
        input.deltaTime);

    if (awarenessState_ == EnemyAwarenessState::Unaware ||
        !lastKnownTargetPosition_.has_value())
    {
        specialChargeHoldTime_ = 0.0F;
        currentMoveDirection_ = Vec2{};
        return EnemyAiDecision{};
    }

    const Vec2 selectedTarget =
        awarenessState_ == EnemyAwarenessState::Alerted
            ? input.targetPosition
            : *lastKnownTargetPosition_;
    const Vec2 targetOffset{
        selectedTarget.x - input.selfPosition.x,
        selectedTarget.y - input.selfPosition.y};
    const float distanceSquared = lengthSquared(targetOffset);
    if (!std::isfinite(distanceSquared) ||
        distanceSquared <= kDirectionEpsilon * kDirectionEpsilon)
    {
        currentMoveDirection_ = Vec2{};
        return EnemyAiDecision{};
    }

    const float distance = std::sqrt(distanceSquared);
    const Vec2 direction{
        targetOffset.x / distance,
        targetOffset.y / distance};
    const Vec2 movementTarget =
        input.navigationTarget.value_or(selectedTarget);
    const Vec2 movementDirection = normalizedOrZero(
        Vec2{
            movementTarget.x - input.selfPosition.x,
            movementTarget.y - input.selfPosition.y});

    const bool canAttack =
        awarenessState_ == EnemyAwarenessState::Alerted &&
        input.tactical.role == EnemyTacticalRole::Engage &&
        input.tactical.canStartAttack;
    const bool inSpecialChargeBand =
        distance >= config_.specialChargeMinDistance &&
        distance <= config_.specialChargeMaxDistance;

    if (canAttack &&
        specialChargeArmed_ &&
        inSpecialChargeBand)
    {
        if (std::isfinite(input.deltaTime) &&
            input.deltaTime > 0.0F)
        {
            specialChargeHoldTime_ = std::min(
                config_.specialChargeHoldDuration,
                specialChargeHoldTime_ + input.deltaTime);
        }

        if (specialChargeHoldTime_ >=
                config_.specialChargeHoldDuration &&
            attackReady(EnemyAttackType::Grab))
        {
            currentMoveDirection_ = updateMoveDirection(
                direction,
                input.deltaTime);
            return EnemyAiDecision{
                Vec2{},
                EnemyAttackType::Grab};
        }
    }
    else
    {
        specialChargeHoldTime_ = 0.0F;
    }

    if (canAttack &&
        distance <= config_.scratchAttackDistance &&
        attackReady(EnemyAttackType::Scratch))
    {
        currentMoveDirection_ = updateMoveDirection(
            direction,
            input.deltaTime);
        return EnemyAiDecision{
            Vec2{},
            EnemyAttackType::Scratch};
    }

    Vec2 desiredDirection{};
    if (awarenessState_ == EnemyAwarenessState::Searching)
    {
        desiredDirection = movementDirection;
    }
    else if (input.tactical.role == EnemyTacticalRole::Support)
    {
        const float side =
            input.tactical.supportSide < 0.0F
                ? -1.0F
                : 1.0F;
        const Vec2 lateral{
            -direction.y * side,
            direction.x * side};

        if (distance < config_.supportMinDistance)
        {
            desiredDirection = Vec2{
                -direction.x + lateral.x * 0.35F,
                -direction.y + lateral.y * 0.35F};
        }
        else if (distance > config_.supportMaxDistance)
        {
            desiredDirection = Vec2{
                direction.x + lateral.x * 0.25F,
                direction.y + lateral.y * 0.25F};
        }
        else
        {
            desiredDirection = lateral;
        }
    }
    else if (distance > config_.stopDistance)
    {
        desiredDirection = movementDirection;
    }

    desiredDirection.x +=
        input.tactical.separationDirection.x;
    desiredDirection.y +=
        input.tactical.separationDirection.y;

    return EnemyAiDecision{
        updateMoveDirection(
            normalizedOrZero(desiredDirection),
            input.deltaTime),
        std::nullopt};
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

void EnemyAiState::hearTarget(Vec2 targetPosition) noexcept
{
    if (!std::isfinite(targetPosition.x) ||
        !std::isfinite(targetPosition.y))
    {
        return;
    }
    awarenessState_ = EnemyAwarenessState::Alerted;
    lastKnownTargetPosition_ = targetPosition;
    searchTimeRemaining_ = config_.searchMemoryDuration;
}

void EnemyAiState::reset() noexcept
{
    grabCooldownRemaining_ = 0.0F;
    scratchCooldownRemaining_ = 0.0F;
    specialChargeArmed_ = false;
    specialChargeHoldTime_ = 0.0F;
    awarenessState_ = EnemyAwarenessState::Unaware;
    lastKnownTargetPosition_.reset();
    searchTimeRemaining_ = 0.0F;
    currentMoveDirection_ = Vec2{};
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

bool EnemyAiState::hasAttackOpportunity(
    float distance) const noexcept
{
    if (!std::isfinite(distance) || distance < 0.0F)
    {
        return false;
    }

    const bool scratchReady =
        distance <= config_.scratchAttackDistance &&
        attackReady(EnemyAttackType::Scratch);
    const bool grabReady =
        specialChargeArmed_ &&
        distance >= config_.specialChargeMinDistance &&
        distance <= config_.specialChargeMaxDistance &&
        attackReady(EnemyAttackType::Grab);
    return scratchReady || grabReady;
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

EnemyAwarenessState
EnemyAiState::awarenessState() const noexcept
{
    return awarenessState_;
}

std::optional<Vec2>
EnemyAiState::lastKnownTargetPosition() const noexcept
{
    return lastKnownTargetPosition_;
}

float EnemyAiState::searchTimeRemaining() const noexcept
{
    return searchTimeRemaining_;
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

void EnemyAiState::updatePerception(
    Vec2 selfPosition,
    Vec2 targetPosition,
    bool targetVisible,
    float deltaTime) noexcept
{
    const float targetDistance = distanceBetween(
        selfPosition,
        targetPosition);

    if (awarenessState_ == EnemyAwarenessState::Unaware)
    {
        if (targetVisible &&
            targetDistance <= config_.acquireTargetDistance)
        {
            awarenessState_ = EnemyAwarenessState::Alerted;
            lastKnownTargetPosition_ = targetPosition;
        }
        return;
    }

    if (awarenessState_ == EnemyAwarenessState::Alerted)
    {
        if (targetVisible &&
            targetDistance <= config_.loseTargetDistance)
        {
            lastKnownTargetPosition_ = targetPosition;
            return;
        }

        awarenessState_ = EnemyAwarenessState::Searching;
        searchTimeRemaining_ = config_.searchMemoryDuration;
    }
    else if (targetVisible &&
             targetDistance <= config_.acquireTargetDistance)
    {
        awarenessState_ = EnemyAwarenessState::Alerted;
        lastKnownTargetPosition_ = targetPosition;
        searchTimeRemaining_ = 0.0F;
        return;
    }

    if (!lastKnownTargetPosition_.has_value())
    {
        awarenessState_ = EnemyAwarenessState::Unaware;
        searchTimeRemaining_ = 0.0F;
        return;
    }

    const float lastKnownDistance = distanceBetween(
        selfPosition,
        *lastKnownTargetPosition_);
    if (lastKnownDistance <= config_.searchArrivalDistance)
    {
        awarenessState_ = EnemyAwarenessState::Unaware;
        lastKnownTargetPosition_.reset();
        searchTimeRemaining_ = 0.0F;
        return;
    }

    if (std::isfinite(deltaTime) &&
        deltaTime > 0.0F)
    {
        searchTimeRemaining_ = std::max(
            0.0F,
            searchTimeRemaining_ - deltaTime);
    }

    if (searchTimeRemaining_ <= 0.0F)
    {
        awarenessState_ = EnemyAwarenessState::Unaware;
        lastKnownTargetPosition_.reset();
        currentMoveDirection_ = Vec2{};
    }
}

Vec2 EnemyAiState::updateMoveDirection(
    Vec2 desiredDirection,
    float deltaTime) noexcept
{
    desiredDirection = normalizedOrZero(desiredDirection);
    if (lengthSquared(desiredDirection) <=
        kDirectionEpsilon * kDirectionEpsilon)
    {
        currentMoveDirection_ = Vec2{};
        return currentMoveDirection_;
    }

    if (lengthSquared(currentMoveDirection_) <=
        kDirectionEpsilon * kDirectionEpsilon)
    {
        currentMoveDirection_ = desiredDirection;
        return currentMoveDirection_;
    }

    if (!std::isfinite(deltaTime) ||
        deltaTime <= 0.0F)
    {
        return currentMoveDirection_;
    }

    const float currentAngle = std::atan2(
        currentMoveDirection_.y,
        currentMoveDirection_.x);
    const float desiredAngle = std::atan2(
        desiredDirection.y,
        desiredDirection.x);
    const float angleDelta = wrappedAngle(
        desiredAngle - currentAngle);
    const float maximumStep =
        config_.maximumTurnRateRadians * deltaTime;
    const float nextAngle = currentAngle + std::clamp(
        angleDelta,
        -maximumStep,
        maximumStep);

    currentMoveDirection_ = Vec2{
        std::cos(nextAngle),
        std::sin(nextAngle)};
    return currentMoveDirection_;
}

bool EnemyAiState::attackReady(
    EnemyAttackType type) const noexcept
{
    return cooldownRemaining(type) <= 0.0F;
}
