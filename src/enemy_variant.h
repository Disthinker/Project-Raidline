#pragma once

#include <cstdint>
#include <optional>
#include <string_view>
#include "enemy_armor.h"

// Persist the namespaced identities below, never these in-memory enum codes.
// This small value owns frozen movement, not perception or activity policy.
enum class EnemyVariantKind : std::uint32_t
{
    Basic,
    Armored,
    Runner
};

inline constexpr std::uint32_t kStandardInfectedAttackProfileVersion = 1U;
inline constexpr std::string_view kStandardInfectedAttackProfileId{
    "enemy.attack.infected-standard-v1"};

struct EnemyMovementProfile
{
    float normalMoveSpeed{72.0F};
    std::optional<float> visiblePursuitMoveSpeed;
    std::uint32_t attackProfileVersion{kStandardInfectedAttackProfileVersion};
    friend bool operator==(const EnemyMovementProfile &, const EnemyMovementProfile &) = default;
};

struct EnemyVariantSnapshot
{
    EnemyVariantKind kind{EnemyVariantKind::Basic};
    EnemyMovementProfile movement;
    friend bool operator==(const EnemyVariantSnapshot &, const EnemyVariantSnapshot &) = default;
};

[[nodiscard]] inline std::string_view enemyVariantDefinitionId(EnemyVariantKind kind) noexcept
{
    switch (kind)
    {
    case EnemyVariantKind::Basic: return "enemy.infected.basic";
    case EnemyVariantKind::Armored: return "enemy.infected.armored";
    case EnemyVariantKind::Runner: return "enemy.infected.runner";
    }
    return {};
}

[[nodiscard]] inline std::optional<EnemyVariantKind> enemyVariantKindFromDefinitionId(
    std::string_view id) noexcept
{
    if (id == "enemy.infected.basic") return EnemyVariantKind::Basic;
    if (id == "enemy.infected.armored") return EnemyVariantKind::Armored;
    if (id == "enemy.infected.runner") return EnemyVariantKind::Runner;
    return std::nullopt;
}

[[nodiscard]] inline EnemyVariantSnapshot ordinaryEnemyVariantSnapshot(bool armored = false) noexcept
{
    return {armored ? EnemyVariantKind::Armored : EnemyVariantKind::Basic,
        {72.0F, std::nullopt, kStandardInfectedAttackProfileVersion}};
}

[[nodiscard]] inline EnemyVariantSnapshot runnerEnemyVariantSnapshot() noexcept
{
    return {EnemyVariantKind::Runner,
        {90.0F, 270.0F, kStandardInfectedAttackProfileVersion}};
}

// A versioned snapshot must keep its original values. Future balance changes
// need a new contract, not a load-time lookup that silently replaces them.
[[nodiscard]] inline bool validEnemyVariantSnapshot(const EnemyVariantSnapshot &value) noexcept
{
    switch (value.kind)
    {
    case EnemyVariantKind::Basic: return value == ordinaryEnemyVariantSnapshot();
    case EnemyVariantKind::Armored: return value == ordinaryEnemyVariantSnapshot(true);
    case EnemyVariantKind::Runner: return value == runnerEnemyVariantSnapshot();
    }
    return false;
}

[[nodiscard]] inline bool isRunnerEnemyVariant(
    const std::optional<EnemyVariantSnapshot> &value) noexcept
{
    return value && value->kind == EnemyVariantKind::Runner;
}

[[nodiscard]] inline bool validEnemyVariantVitals(const EnemyVariantSnapshot &value,
    int maximumHealth, const std::optional<EnemyArmorState> &armor) noexcept
{
    if (!validEnemyVariantSnapshot(value)) return false;
    if (value.kind == EnemyVariantKind::Runner) return maximumHealth == 8 && !armor;
    if (value.kind == EnemyVariantKind::Basic) return maximumHealth == 12 && !armor;
    return maximumHealth == 12 && armor && armor->valid() &&
        armor->protectionRequirement == 7 && armor->durability <= 9 &&
        armor->durabilityLossBasisPoints == 10000;
}
