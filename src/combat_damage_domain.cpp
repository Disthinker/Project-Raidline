#include "combat_damage_domain.h"

#include <algorithm>
#include <cstdint>
#include <limits>

namespace
{
    constexpr std::int64_t kBasisPoints{10000};
    constexpr std::int64_t kMinimumArmorDamageBasisPoints{1000};

    std::int64_t regionMultiplierBasisPoints(HitRegion region) noexcept
    {
        switch (region)
        {
        case HitRegion::Head:
            return 20000;
        case HitRegion::Torso:
            return 10000;
        case HitRegion::Legs:
            return 7500;
        }
        return 10000;
    }

    int multiplyRoundedUp(int value, std::int64_t basisPoints) noexcept
    {
        const std::int64_t product =
            static_cast<std::int64_t>(value) * basisPoints;
        const std::int64_t rounded =
            (product + kBasisPoints - 1) / kBasisPoints;
        return static_cast<int>(std::clamp<std::int64_t>(
            rounded,
            1,
            std::numeric_limits<int>::max()));
    }

    bool validArmor(const ArmorProtectionView &armor) noexcept
    {
        return armor.protectionRequirement > 0 &&
               armor.durabilityLossBasisPoints > 0;
    }
}

bool CombatDamageResolution::resolved() const noexcept
{
    return status == CombatDamageStatus::Resolved;
}

CombatDamageResolution resolveCombatDamage(
    const CombatDamageCommand &command) noexcept
{
    CombatDamageResolution result{};
    result.region = command.region;
    result.semantic = command.weakPoint
        ? HitSemantic::WeakPoint
        : command.region == HitRegion::Head
            ? HitSemantic::Headshot
            : HitSemantic::Normal;

    if (command.baseDamage <= 0)
    {
        result.status = CombatDamageStatus::RejectedInvalidDamage;
        return result;
    }
    if (command.penetration < 0)
    {
        result.status = CombatDamageStatus::RejectedInvalidPenetration;
        return result;
    }
    if (command.armorDamage < 0)
    {
        result.status = CombatDamageStatus::RejectedInvalidArmorDamage;
        return result;
    }
    if (command.armor.has_value() && !validArmor(*command.armor))
    {
        result.status = CombatDamageStatus::RejectedInvalidArmor;
        return result;
    }

    result.damageBeforeArmor = multiplyRoundedUp(
        command.baseDamage,
        regionMultiplierBasisPoints(command.region));
    if (command.weakPoint)
    {
        result.damageBeforeArmor = multiplyRoundedUp(
            result.damageBeforeArmor,
            15000);
    }
    result.damageApplied = result.damageBeforeArmor;

    if (!command.armor.has_value() ||
        command.armor->coverage != command.region ||
        command.armor->currentDurability == 0)
    {
        result.status = CombatDamageStatus::Resolved;
        return result;
    }

    result.armorCovered = true;
    if (command.penetration >= command.armor->protectionRequirement)
    {
        result.status = CombatDamageStatus::Resolved;
        return result;
    }

    const std::int64_t retainedDamageBasisPoints = std::max(
        kMinimumArmorDamageBasisPoints,
        static_cast<std::int64_t>(command.penetration) * kBasisPoints /
            command.armor->protectionRequirement);
    result.damageApplied = multiplyRoundedUp(
        result.damageBeforeArmor,
        retainedDamageBasisPoints);
    result.armorReducedDamage =
        result.damageApplied < result.damageBeforeArmor;

    if (result.armorReducedDamage && command.armorDamage > 0)
    {
        const std::int64_t rawLoss =
            static_cast<std::int64_t>(command.armorDamage) *
            command.armor->durabilityLossBasisPoints;
        const std::uint32_t roundedLoss = static_cast<std::uint32_t>(
            std::max<std::int64_t>(
                1,
                (rawLoss + kBasisPoints - 1) / kBasisPoints));
        result.armorDurabilityLoss = std::min(
            command.armor->currentDurability,
            roundedLoss);
    }

    result.status = CombatDamageStatus::Resolved;
    return result;
}
