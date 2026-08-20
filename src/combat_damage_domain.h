#pragma once

#include <cstdint>
#include <optional>

enum class HitRegion
{
    Head,
    Torso,
    Legs
};

enum class HitSemantic
{
    Normal,
    Headshot,
    WeakPoint
};

struct ArmorProtectionView
{
    HitRegion coverage{HitRegion::Torso};
    int protectionRequirement{};
    std::uint32_t currentDurability{};
    std::uint32_t durabilityLossBasisPoints{10000};
};

struct CombatDamageCommand
{
    int baseDamage{};
    HitRegion region{HitRegion::Torso};
    int penetration{};
    int armorDamage{};
    bool weakPoint{};
    std::optional<ArmorProtectionView> armor;
};

enum class CombatDamageStatus
{
    Resolved,
    RejectedInvalidDamage,
    RejectedInvalidPenetration,
    RejectedInvalidArmorDamage,
    RejectedInvalidArmor
};

struct CombatDamageResolution
{
    CombatDamageStatus status{CombatDamageStatus::RejectedInvalidDamage};
    HitRegion region{HitRegion::Torso};
    HitSemantic semantic{HitSemantic::Normal};
    int damageBeforeArmor{};
    int damageApplied{};
    bool armorCovered{};
    bool armorReducedDamage{};
    std::uint32_t armorDurabilityLoss{};

    [[nodiscard]] bool resolved() const noexcept;
};

// Pure deterministic query. The caller owns HP and armor state and applies the
// returned values atomically only after every participant has been revalidated.
[[nodiscard]] CombatDamageResolution resolveCombatDamage(
    const CombatDamageCommand &command) noexcept;
