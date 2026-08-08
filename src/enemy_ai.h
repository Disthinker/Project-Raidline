#pragma once

#include <optional>

#include "enemy_attack.h"
#include "vec2.h"

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
        Vec2 targetOffset,
        bool canStartAttack,
        float deltaTime) noexcept;

    void recordAttackStarted(
        EnemyAttackType type) noexcept;

    void reset() noexcept;

    [[nodiscard]]
    const EnemyAiConfig &config() const noexcept;

    [[nodiscard]]
    float cooldownRemaining(
        EnemyAttackType type) const noexcept;

    [[nodiscard]]
    bool specialChargeArmed() const noexcept;

    [[nodiscard]]
    float specialChargeHoldRemaining() const noexcept;

private:
    EnemyAiConfig config_;
    float grabCooldownRemaining_{};
    float scratchCooldownRemaining_{};
    bool specialChargeArmed_{};
    float specialChargeHoldTime_{};

    void advanceCooldowns(float deltaTime) noexcept;

    [[nodiscard]]
    bool attackReady(
        EnemyAttackType type) const noexcept;
};
