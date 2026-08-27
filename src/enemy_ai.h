#pragma once

#include <optional>

#include "enemy_attack.h"
#include "vec2.h"

enum class EnemyAwarenessState
{
    Unaware,
    Alerted,
    Searching
};

enum class EnemyTacticalRole
{
    Engage,
    Pressure,
    Support
};

[[nodiscard]]
const char *enemyAwarenessStateName(
    EnemyAwarenessState state) noexcept;

[[nodiscard]]
const char *enemyTacticalRoleName(
    EnemyTacticalRole role) noexcept;

struct EnemyAiConfig
{
    float normalMoveSpeed{72.0F};
    float attackMoveSpeed{135.0F};
    float stopDistance{48.0F};
    float scratchAttackDistance{76.0F};
    float specialChargeMinDistance{100.0F};
    float specialChargeMaxDistance{170.0F};
    float specialChargeHoldDuration{0.50F};
    float grabCooldown{4.00F};
    float scratchCooldown{0.65F};

    float acquireTargetDistance{360.0F};
    float loseTargetDistance{460.0F};
    float searchMemoryDuration{8.0F};
    float searchArrivalDistance{20.0F};
    float maximumTurnRateRadians{9.42477796F};
    float supportMinDistance{105.0F};
    float supportMaxDistance{155.0F};
};

struct EnemyTacticalDirective
{
    EnemyTacticalRole role{EnemyTacticalRole::Support};
    bool canStartAttack{};
    Vec2 separationDirection{};
    float supportSide{1.0F};
};

struct EnemyAiInput
{
    Vec2 selfPosition{};
    Vec2 targetPosition{};
    EnemyTacticalDirective tactical{};
    float deltaTime{};
    bool targetVisible{true};
    std::optional<Vec2> navigationTarget;
};

struct EnemyAiDecision
{
    Vec2 moveDirection{};
    std::optional<EnemyAttackType> attackRequest;
};

class EnemyAiState
{
public:
    EnemyAiState();
    explicit EnemyAiState(EnemyAiConfig config);

    [[nodiscard]]
    EnemyAiDecision update(
        const EnemyAiInput &input) noexcept;

    void recordAttackStarted(
        EnemyAttackType type) noexcept;

    void hearTarget(Vec2 targetPosition) noexcept;

    void reset() noexcept;

    [[nodiscard]]
    const EnemyAiConfig &config() const noexcept;

    [[nodiscard]]
    float cooldownRemaining(
        EnemyAttackType type) const noexcept;

    [[nodiscard]]
    bool hasAttackOpportunity(
        float distance) const noexcept;

    [[nodiscard]]
    bool specialChargeArmed() const noexcept;

    [[nodiscard]]
    float specialChargeHoldRemaining() const noexcept;

    [[nodiscard]]
    EnemyAwarenessState awarenessState() const noexcept;

    [[nodiscard]]
    std::optional<Vec2> lastKnownTargetPosition() const noexcept;

    [[nodiscard]]
    float searchTimeRemaining() const noexcept;

private:
    EnemyAiConfig config_;
    float grabCooldownRemaining_{};
    float scratchCooldownRemaining_{};
    bool specialChargeArmed_{};
    float specialChargeHoldTime_{};
    EnemyAwarenessState awarenessState_{EnemyAwarenessState::Unaware};
    std::optional<Vec2> lastKnownTargetPosition_;
    float searchTimeRemaining_{};
    Vec2 currentMoveDirection_{};

    void advanceCooldowns(float deltaTime) noexcept;

    void updatePerception(
        Vec2 selfPosition,
        Vec2 targetPosition,
        bool targetVisible,
        float deltaTime) noexcept;

    [[nodiscard]]
    Vec2 updateMoveDirection(
        Vec2 desiredDirection,
        float deltaTime) noexcept;

    [[nodiscard]]
    bool attackReady(
        EnemyAttackType type) const noexcept;
};
