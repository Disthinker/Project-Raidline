#include <gtest/gtest.h>

#include <cmath>
#include <limits>
#include <stdexcept>

#include "enemy_attack.h"

namespace
{
    constexpr float kTolerance{0.0001F};
}

TEST(EnemyAttackStateTest, DefaultConfigsMatchWeek27Contract)
{
    EnemyAttackState attacks;

    const EnemyAttackConfig &grab =
        attacks.config(EnemyAttackType::Grab);
    EXPECT_FLOAT_EQ(grab.windupDuration, 0.55F);
    EXPECT_FLOAT_EQ(grab.activeDuration, 0.55F);
    EXPECT_FLOAT_EQ(grab.missRecoveryDuration, 1.35F);
    EXPECT_FLOAT_EQ(grab.lungeDistance, 74.25F);
    EXPECT_EQ(grab.damage, 0);
    EXPECT_FLOAT_EQ(grab.controlDuration, 0.0F);

    const EnemyAttackConfig &scratch =
        attacks.config(EnemyAttackType::Scratch);
    EXPECT_FLOAT_EQ(scratch.windupDuration, 0.18F);
    EXPECT_FLOAT_EQ(scratch.lungeDistance, 0.0F);
    EXPECT_EQ(scratch.damage, 1);

    const EnemyAttackConfig &bite =
        attacks.config(EnemyAttackType::Bite);
    EXPECT_FLOAT_EQ(bite.windupDuration, 0.25F);
    EXPECT_EQ(bite.damage, 2);
    EXPECT_FLOAT_EQ(bite.controlDuration, 0.75F);
}

TEST(EnemyAttackStateTest, ProductionDamageCarriesExplicitArmorSemantics)
{
    const EnemyAttackCombatDamage scratch = enemyAttackCombatDamage(
        EnemyAttackType::Scratch);
    EXPECT_EQ(scratch.baseDamage, 12);
    EXPECT_EQ(scratch.region, HitRegion::Torso);
    EXPECT_EQ(scratch.penetration, 1);
    EXPECT_EQ(scratch.armorDamage, 2);

    const EnemyAttackCombatDamage bite = enemyAttackCombatDamage(
        EnemyAttackType::Bite);
    EXPECT_EQ(bite.baseDamage, 18);
    EXPECT_EQ(bite.region, HitRegion::Head);
    EXPECT_EQ(bite.penetration, 1);
    EXPECT_EQ(bite.armorDamage, 3);

    EXPECT_EQ(
        enemyAttackCombatDamage(EnemyAttackType::Grab).baseDamage,
        0);
}

TEST(EnemyAttackStateTest, StartLocksNormalizedDirectionAndWindup)
{
    EnemyAttackState attacks;

    ASSERT_TRUE(attacks.tryStart(
        EnemyAttackType::Grab,
        Vec2{3.0F, 4.0F}));

    ASSERT_EQ(attacks.type(), EnemyAttackType::Grab);
    EXPECT_EQ(attacks.phase(), EnemyAttackPhase::Windup);
    EXPECT_NEAR(attacks.direction().x, 0.6F, kTolerance);
    EXPECT_NEAR(attacks.direction().y, 0.8F, kTolerance);
    EXPECT_FLOAT_EQ(attacks.phaseRemaining(), 0.55F);
}

TEST(EnemyAttackStateTest, InvalidDirectionAndActiveReplacementAreRejected)
{
    EnemyAttackState attacks;

    EXPECT_FALSE(attacks.tryStart(
        EnemyAttackType::Scratch,
        Vec2{}));
    EXPECT_FALSE(attacks.tryStart(
        EnemyAttackType::Scratch,
        Vec2{
            std::numeric_limits<float>::quiet_NaN(),
            1.0F}));
    ASSERT_TRUE(attacks.tryStart(
        EnemyAttackType::Grab,
        Vec2{1.0F, 0.0F}));

    EXPECT_FALSE(attacks.tryStart(
        EnemyAttackType::Bite,
        Vec2{-1.0F, 0.0F}));
    ASSERT_EQ(attacks.type(), EnemyAttackType::Grab);
    EXPECT_EQ(attacks.phase(), EnemyAttackPhase::Windup);
}

TEST(EnemyAttackStateTest, InvalidEnumValueDoesNotEnterAttackState)
{
    EnemyAttackState attack;

    EXPECT_FALSE(attack.tryStart(
        static_cast<EnemyAttackType>(255),
        Vec2{1.0F, 0.0F}));
    EXPECT_EQ(attack.phase(), EnemyAttackPhase::Idle);
    EXPECT_FALSE(attack.type().has_value());
}

TEST(EnemyAttackStateTest, PhasesAdvanceInStrictOrder)
{
    EnemyAttackState attacks;
    ASSERT_TRUE(attacks.tryStart(
        EnemyAttackType::Scratch,
        Vec2{1.0F, 0.0F}));

    EXPECT_EQ(attacks.phase(), EnemyAttackPhase::Windup);
    static_cast<void>(attacks.update(0.18F));
    EXPECT_EQ(attacks.phase(), EnemyAttackPhase::Active);
    static_cast<void>(attacks.update(0.10F));
    EXPECT_EQ(attacks.phase(), EnemyAttackPhase::Recovery);
    static_cast<void>(attacks.update(0.28F));
    EXPECT_EQ(attacks.phase(), EnemyAttackPhase::Idle);
    EXPECT_FALSE(attacks.type().has_value());
    EXPECT_FLOAT_EQ(attacks.phaseRemaining(), 0.0F);
}

TEST(EnemyAttackStateTest, GrabLungeDistanceIsFramePartitionIndependent)
{
    EnemyAttackState singleStep;
    EnemyAttackState splitSteps;
    ASSERT_TRUE(singleStep.tryStart(
        EnemyAttackType::Grab,
        Vec2{1.0F, 0.0F}));
    ASSERT_TRUE(splitSteps.tryStart(
        EnemyAttackType::Grab,
        Vec2{1.0F, 0.0F}));

    const EnemyAttackAdvance singleAdvance =
        singleStep.update(2.45F);
    float splitDistance{};
    for (int index = 0; index < 245; ++index)
    {
        splitDistance +=
            splitSteps.update(0.01F).lungeDistance;
    }

    EXPECT_NEAR(singleAdvance.lungeDistance, 74.25F, kTolerance);
    EXPECT_NEAR(splitDistance, 74.25F, 0.001F);
    EXPECT_EQ(singleStep.phase(), EnemyAttackPhase::Idle);
    EXPECT_EQ(splitSteps.phase(), EnemyAttackPhase::Idle);
}

TEST(EnemyAttackStateTest, HitOpportunityExistsOnlyDuringActiveAndConsumesOnce)
{
    EnemyAttackState attacks;
    ASSERT_TRUE(attacks.tryStart(
        EnemyAttackType::Bite,
        Vec2{1.0F, 0.0F}));

    EXPECT_FALSE(attacks.hasHitOpportunity());
    EXPECT_FALSE(attacks.tryConsumeHit());

    static_cast<void>(attacks.update(0.25F));
    ASSERT_TRUE(attacks.hasHitOpportunity());
    EXPECT_TRUE(attacks.tryConsumeHit());
    EXPECT_TRUE(attacks.hitConsumed());
    EXPECT_FALSE(attacks.tryConsumeHit());

    static_cast<void>(attacks.update(0.12F));
    EXPECT_FALSE(attacks.hasHitOpportunity());
}

TEST(EnemyAttackStateTest, ActiveTimeCrossingIntoRecoveryCanStillBeConsumed)
{
    EnemyAttackState attacks;
    ASSERT_TRUE(attacks.tryStart(
        EnemyAttackType::Scratch,
        Vec2{1.0F, 0.0F}));

    const EnemyAttackAdvance advance =
        attacks.update(0.29F);

    EXPECT_TRUE(advance.hadActiveTime);
    EXPECT_EQ(attacks.phase(), EnemyAttackPhase::Recovery);
    EXPECT_TRUE(attacks.hasHitOpportunity());
    EXPECT_TRUE(attacks.tryConsumeHit());
}

TEST(EnemyAttackStateTest, NonPositiveOrNonFiniteDeltaDoesNotAdvance)
{
    EnemyAttackState attacks;
    ASSERT_TRUE(attacks.tryStart(
        EnemyAttackType::Grab,
        Vec2{1.0F, 0.0F}));

    static_cast<void>(attacks.update(0.0F));
    static_cast<void>(attacks.update(-1.0F));
    static_cast<void>(attacks.update(
        std::numeric_limits<float>::infinity()));

    EXPECT_EQ(attacks.phase(), EnemyAttackPhase::Windup);
    EXPECT_FLOAT_EQ(attacks.phaseRemaining(), 0.55F);
}

TEST(EnemyAttackStateTest, GrabWindupTracksThenLocksLatestDirection)
{
    EnemyAttackState attacks;
    ASSERT_TRUE(attacks.tryStart(
        EnemyAttackType::Grab,
        Vec2{1.0F, 0.0F}));

    ASSERT_TRUE(attacks.trackDirection(Vec2{0.0F, 2.0F}));
    EXPECT_FLOAT_EQ(attacks.direction().x, 0.0F);
    EXPECT_FLOAT_EQ(attacks.direction().y, 1.0F);
    static_cast<void>(attacks.update(0.55F));
    ASSERT_EQ(attacks.phase(), EnemyAttackPhase::Active);

    EXPECT_FALSE(attacks.trackDirection(Vec2{-1.0F, 0.0F}));
    EXPECT_FLOAT_EQ(attacks.direction().x, 0.0F);
    EXPECT_FLOAT_EQ(attacks.direction().y, 1.0F);
}

TEST(EnemyAttackStateTest, GrabContactConvertsToSingleBiteHit)
{
    EnemyAttackState attacks;
    ASSERT_TRUE(attacks.tryStart(
        EnemyAttackType::Grab,
        Vec2{1.0F, 0.0F}));
    static_cast<void>(attacks.update(0.55F));

    EXPECT_TRUE(attacks.hasGrabContactOpportunity());
    EXPECT_FALSE(attacks.hasHitOpportunity());
    EXPECT_FALSE(attacks.tryConsumeHit());
    ASSERT_TRUE(attacks.tryConfirmGrabContact());

    ASSERT_EQ(attacks.type(), EnemyAttackType::Bite);
    EXPECT_EQ(attacks.phase(), EnemyAttackPhase::Active);
    ASSERT_TRUE(attacks.currentConfig().has_value());
    EXPECT_EQ(attacks.currentConfig()->damage, 2);
    EXPECT_FLOAT_EQ(attacks.currentConfig()->controlDuration, 0.75F);
    EXPECT_TRUE(attacks.hasHitOpportunity());
    EXPECT_TRUE(attacks.tryConsumeHit());
    EXPECT_FALSE(attacks.tryConsumeHit());
}

TEST(EnemyAttackStateTest, MissedGrabFallsIntoLongOffBalance)
{
    EnemyAttackState attacks;
    ASSERT_TRUE(attacks.tryStart(
        EnemyAttackType::Grab,
        Vec2{1.0F, 0.0F}));

    static_cast<void>(attacks.update(1.10F));
    ASSERT_EQ(attacks.phase(), EnemyAttackPhase::OffBalance);
    EXPECT_FLOAT_EQ(attacks.phaseRemaining(), 1.35F);
    EXPECT_FALSE(attacks.hasHitOpportunity());

    // The frame that crossed Active still offers collision submission after
    // movement. The next OffBalance step clears that one-frame opportunity.
    EXPECT_TRUE(attacks.hasGrabContactOpportunity());
    static_cast<void>(attacks.update(0.01F));
    EXPECT_FALSE(attacks.hasGrabContactOpportunity());

    static_cast<void>(attacks.update(1.35F));
    EXPECT_EQ(attacks.phase(), EnemyAttackPhase::Idle);
    EXPECT_FALSE(attacks.type().has_value());
}

TEST(EnemyAttackStateTest, InvalidAttackSpecificConfigIsRejected)
{
    EnemyAttackConfigSet configs =
        defaultEnemyAttackConfigs();
    configs[0].lungeDistance = 0.0F;

    EXPECT_THROW(
        static_cast<void>(EnemyAttackState{configs}),
        std::invalid_argument);

    configs = defaultEnemyAttackConfigs();
    configs[2].controlDuration = 0.0F;
    EXPECT_THROW(
        static_cast<void>(EnemyAttackState{configs}),
        std::invalid_argument);

    configs = defaultEnemyAttackConfigs();
    configs[0].damage = 1;
    EXPECT_THROW(
        static_cast<void>(EnemyAttackState{configs}),
        std::invalid_argument);

    configs = defaultEnemyAttackConfigs();
    configs[1].missRecoveryDuration = 1.0F;
    EXPECT_THROW(
        static_cast<void>(EnemyAttackState{configs}),
        std::invalid_argument);
}

TEST(EnemyAttackStateTest, DebugNamesAreStable)
{
    EXPECT_STREQ(
        enemyAttackTypeName(EnemyAttackType::Grab),
        "Grab");
    EXPECT_STREQ(
        enemyAttackTypeName(EnemyAttackType::Scratch),
        "Scratch");
    EXPECT_STREQ(
        enemyAttackTypeName(EnemyAttackType::Bite),
        "Bite");
    EXPECT_STREQ(
        enemyAttackPhaseName(EnemyAttackPhase::Windup),
        "Windup");
    EXPECT_STREQ(
        enemyAttackPhaseName(EnemyAttackPhase::OffBalance),
        "OffBalance");
}
