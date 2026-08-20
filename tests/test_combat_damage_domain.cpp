#include <gtest/gtest.h>

#include "combat_damage_domain.h"

TEST(CombatDamageDomainTest, RegionsUseConfirmedDamageMultipliers)
{
    const auto head = resolveCombatDamage(
        CombatDamageCommand{20, HitRegion::Head, 1, 10});
    const auto torso = resolveCombatDamage(
        CombatDamageCommand{20, HitRegion::Torso, 1, 10});
    const auto legs = resolveCombatDamage(
        CombatDamageCommand{20, HitRegion::Legs, 1, 10});

    ASSERT_TRUE(head.resolved());
    ASSERT_TRUE(torso.resolved());
    ASSERT_TRUE(legs.resolved());
    EXPECT_EQ(head.damageApplied, 40);
    EXPECT_EQ(torso.damageApplied, 20);
    EXPECT_EQ(legs.damageApplied, 15);
    EXPECT_EQ(head.semantic, HitSemantic::Headshot);
    EXPECT_EQ(torso.semantic, HitSemantic::Normal);
}

TEST(CombatDamageDomainTest, WeakPointIsExplicitAndDoesNotDependOnUi)
{
    const auto result = resolveCombatDamage(
        CombatDamageCommand{20, HitRegion::Torso, 1, 10, true});

    ASSERT_TRUE(result.resolved());
    EXPECT_EQ(result.semantic, HitSemantic::WeakPoint);
    EXPECT_EQ(result.damageBeforeArmor, 30);
    EXPECT_EQ(result.damageApplied, 30);
}

TEST(CombatDamageDomainTest, MatchingArmorUsesDeterministicPenetrationReduction)
{
    const auto result = resolveCombatDamage(
        CombatDamageCommand{
            20,
            HitRegion::Torso,
            2,
            8,
            false,
            ArmorProtectionView{HitRegion::Torso, 4, 100, 15000}});

    ASSERT_TRUE(result.resolved());
    EXPECT_TRUE(result.armorCovered);
    EXPECT_TRUE(result.armorReducedDamage);
    EXPECT_EQ(result.damageBeforeArmor, 20);
    EXPECT_EQ(result.damageApplied, 10);
    EXPECT_EQ(result.armorDurabilityLoss, 12U);
}

TEST(CombatDamageDomainTest, PenetrationMeetingRequirementDoesNotWearArmor)
{
    const auto result = resolveCombatDamage(
        CombatDamageCommand{
            20,
            HitRegion::Head,
            4,
            8,
            false,
            ArmorProtectionView{HitRegion::Head, 4, 100}});

    ASSERT_TRUE(result.resolved());
    EXPECT_TRUE(result.armorCovered);
    EXPECT_FALSE(result.armorReducedDamage);
    EXPECT_EQ(result.damageApplied, 40);
    EXPECT_EQ(result.armorDurabilityLoss, 0U);
}

TEST(CombatDamageDomainTest, UncoveredOrBrokenArmorCannotReduceDamage)
{
    const auto uncovered = resolveCombatDamage(
        CombatDamageCommand{
            20,
            HitRegion::Legs,
            0,
            8,
            false,
            ArmorProtectionView{HitRegion::Torso, 4, 100}});
    const auto broken = resolveCombatDamage(
        CombatDamageCommand{
            20,
            HitRegion::Torso,
            0,
            8,
            false,
            ArmorProtectionView{HitRegion::Torso, 4, 0}});

    ASSERT_TRUE(uncovered.resolved());
    ASSERT_TRUE(broken.resolved());
    EXPECT_FALSE(uncovered.armorCovered);
    EXPECT_FALSE(broken.armorCovered);
    EXPECT_EQ(uncovered.damageApplied, 15);
    EXPECT_EQ(broken.damageApplied, 20);
}

TEST(CombatDamageDomainTest, ArmorNeverReducesBelowTenPercent)
{
    const auto result = resolveCombatDamage(
        CombatDamageCommand{
            100,
            HitRegion::Torso,
            0,
            1,
            false,
            ArmorProtectionView{HitRegion::Torso, 100, 50}});

    ASSERT_TRUE(result.resolved());
    EXPECT_EQ(result.damageApplied, 10);
    EXPECT_EQ(result.armorDurabilityLoss, 1U);
}

TEST(CombatDamageDomainTest, InvalidInputReturnsNoCommittedPayload)
{
    const auto invalidDamage = resolveCombatDamage(
        CombatDamageCommand{0, HitRegion::Head, 1, 1});
    const auto invalidArmor = resolveCombatDamage(
        CombatDamageCommand{
            10,
            HitRegion::Torso,
            1,
            1,
            false,
            ArmorProtectionView{HitRegion::Torso, 0, 100}});

    EXPECT_EQ(
        invalidDamage.status,
        CombatDamageStatus::RejectedInvalidDamage);
    EXPECT_EQ(invalidDamage.damageApplied, 0);
    EXPECT_EQ(
        invalidArmor.status,
        CombatDamageStatus::RejectedInvalidArmor);
    EXPECT_EQ(invalidArmor.damageApplied, 0);
    EXPECT_EQ(invalidArmor.armorDurabilityLoss, 0U);
}
