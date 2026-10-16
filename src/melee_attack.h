#pragma once
#include "combat_damage_domain.h"
#include "combat_target.h"
#include "item_definition.h"
#include "vec2.h"
#include <optional>
#include <span>

struct MeleeTargetSnapshot
{
    CombatTargetId id{};
    Vec2 center{};
    int health{};
    std::optional<ArmorProtectionView> armor;
    std::size_t rosterPosition{};
    bool reachable{true};
};

struct MeleeAttackResult
{
    bool attempted{}, broken{}, contact{}, committed{}, killed{};
    Vec2 origin{}, direction{1,0}, impact{};
    std::optional<MeleeTargetSnapshot> target;
    CombatDamageResolution damage;
    std::size_t candidatesExamined{};
    std::uint64_t equipmentAssetId{};
};

// Pure query over already-local, current-space candidates. No ammo, RNG,
// sprite orientation, observation privilege, or asset mutation participates.
[[nodiscard]] MeleeAttackResult resolveMeleeAttack(
    Vec2 origin, Vec2 direction, const MeleeUseDefinition &,
    std::span<const MeleeTargetSnapshot> candidates) noexcept;
