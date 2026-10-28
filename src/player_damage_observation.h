#pragma once

#include "combat_target.h"
#include "enemy_attack.h"
#include "enemy_variant.h"
#include "medical_types.h"

// A consumed enemy hit, not permission to attack or a persistent transaction.
// IDs are local to the producing activity; consumers apply its save/failure policy.
struct PlayerDamageObservation
{
    int baseDamage{};
    HitRegion region{HitRegion::Torso};
    int penetration{};
    int armorDamage{};
    bool weakPoint{};
    WoundSource woundSource{WoundSource::None};
    CombatTargetId sourceEnemyId{};
    std::optional<EnemyAttackType> attackType;
    friend bool operator==(const PlayerDamageObservation &,
                           const PlayerDamageObservation &) = default;
};

[[nodiscard]] inline PlayerDamageObservation enemyAttackDamageObservation(
    CombatTargetId source, EnemyAttackType type,
    const std::optional<EnemyVariantSnapshot> &variant = std::nullopt) noexcept
{
    const auto damage = variant &&
        variant->movement.attackProfileVersion == kStandardInfectedAttackProfileVersion
        ? standardInfectedAttackCombatDamageV1(type) : enemyAttackCombatDamage(type);
    return {damage.baseDamage, damage.region, damage.penetration,
            damage.armorDamage, damage.weakPoint,
            type == EnemyAttackType::Scratch ? WoundSource::Scratch :
            type == EnemyAttackType::Bite ? WoundSource::Bite : WoundSource::None,
            source, type};
}
