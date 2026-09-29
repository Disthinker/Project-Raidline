#pragma once
#include "combat_damage_domain.h"
#include <cstdint>

// Enemy combat state, not a player-owned or lootable asset. Coverage is torso only.
struct EnemyArmorState
{
    int protectionRequirement{};
    std::uint32_t durability{};
    std::uint32_t durabilityLossBasisPoints{10000};
    [[nodiscard]] bool valid() const noexcept {
        return protectionRequirement > 0 && protectionRequirement <= 100 &&
            durability <= 1000 && durabilityLossBasisPoints > 0 && durabilityLossBasisPoints <= 10000;
    }
    [[nodiscard]] ArmorProtectionView protection() const noexcept {
        return {HitRegion::Torso, protectionRequirement, durability, durabilityLossBasisPoints};
    }
    friend bool operator==(const EnemyArmorState &, const EnemyArmorState &) = default;
};
