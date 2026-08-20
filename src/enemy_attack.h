#pragma once

#include <array>
#include <cstddef>
#include <optional>

#include "combat_damage_domain.h"
#include "vec2.h"

enum class EnemyAttackType
{
    Grab,
    Scratch,
    Bite
};

enum class EnemyAttackPhase
{
    Idle,
    Windup,
    Active,
    Recovery,
    OffBalance
};

struct EnemyAttackConfig
{
    float windupDuration{};
    float activeDuration{};
    float recoveryDuration{};
    float missRecoveryDuration{};
    float lungeDistance{};
    Vec2 hitboxSize{};
    float hitboxForwardOffset{};
    int damage{};
    float controlDuration{};
};

using EnemyAttackConfigSet =
    std::array<EnemyAttackConfig, 3>;

struct EnemyAttackAdvance
{
    bool hadActiveTime{};
    float lungeDistance{};
};

struct EnemyAttackCombatDamage
{
    int baseDamage{};
    HitRegion region{HitRegion::Torso};
    int penetration{};
    int armorDamage{};
    bool weakPoint{};
};

[[nodiscard]]
EnemyAttackConfigSet defaultEnemyAttackConfigs();

// Production combat semantics stay separate from the legacy three-HP
// simulation adapter. GameSession resolves this result against Profile armor.
[[nodiscard]] EnemyAttackCombatDamage enemyAttackCombatDamage(
    EnemyAttackType type) noexcept;

[[nodiscard]]
const char *enemyAttackTypeName(
    EnemyAttackType type) noexcept;

[[nodiscard]]
const char *enemyAttackPhaseName(
    EnemyAttackPhase phase) noexcept;

class EnemyAttackState
{
public:
    EnemyAttackState();
    explicit EnemyAttackState(
        EnemyAttackConfigSet configs);

    [[nodiscard]]
    bool tryStart(
        EnemyAttackType type,
        Vec2 direction) noexcept;

    [[nodiscard]]
    bool trackDirection(Vec2 direction) noexcept;

    [[nodiscard]]
    EnemyAttackAdvance update(
        float deltaTime) noexcept;

    [[nodiscard]]
    bool tryConsumeHit() noexcept;

    [[nodiscard]]
    bool hasGrabContactOpportunity() const noexcept;

    [[nodiscard]]
    bool tryConfirmGrabContact() noexcept;

    void reset() noexcept;

    [[nodiscard]]
    EnemyAttackPhase phase() const noexcept;

    [[nodiscard]]
    std::optional<EnemyAttackType> type() const noexcept;

    [[nodiscard]]
    Vec2 direction() const noexcept;

    [[nodiscard]]
    float phaseRemaining() const noexcept;

    [[nodiscard]]
    bool hitConsumed() const noexcept;

    [[nodiscard]]
    bool hasHitOpportunity() const noexcept;

    [[nodiscard]]
    const EnemyAttackConfig &config(
        EnemyAttackType type) const noexcept;

    [[nodiscard]]
    std::optional<EnemyAttackConfig>
    currentConfig() const noexcept;

private:
    EnemyAttackConfigSet configs_;
    EnemyAttackPhase phase_{EnemyAttackPhase::Idle};
    std::optional<EnemyAttackType> type_;
    Vec2 direction_{};
    float phaseRemaining_{};
    bool hitConsumed_{};
    bool activeOpportunityPending_{};

    [[nodiscard]]
    static std::size_t indexOf(
        EnemyAttackType type) noexcept;

    void advancePhase() noexcept;
};
