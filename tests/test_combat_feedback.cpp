#include <gtest/gtest.h>

#include <limits>
#include <stdexcept>

#include "combat_feedback.h"

TEST(CombatFeedbackTest, DefaultsStartClear)
{
    const CombatFeedbackState feedback;

    EXPECT_FLOAT_EQ(feedback.muzzleFlashIntensity(), 0.0F);
    EXPECT_FLOAT_EQ(feedback.hitConfirmIntensity(), 0.0F);
    EXPECT_FLOAT_EQ(feedback.playerDamagePulseIntensity(), 0.0F);
    EXPECT_FLOAT_EQ(feedback.muzzleOrigin().x, 0.0F);
    EXPECT_FLOAT_EQ(feedback.muzzleDirection().x, 0.0F);
}

TEST(CombatFeedbackTest, ValidShotNormalizesDirectionAndStoresOrigin)
{
    CombatFeedbackState feedback;

    ASSERT_TRUE(feedback.recordShot({12.0F, 34.0F}, {3.0F, 4.0F}));

    EXPECT_FLOAT_EQ(feedback.muzzleFlashIntensity(), 1.0F);
    EXPECT_FLOAT_EQ(feedback.muzzleOrigin().x, 12.0F);
    EXPECT_FLOAT_EQ(feedback.muzzleOrigin().y, 34.0F);
    EXPECT_FLOAT_EQ(feedback.muzzleDirection().x, 0.6F);
    EXPECT_FLOAT_EQ(feedback.muzzleDirection().y, 0.8F);
}

TEST(CombatFeedbackTest, InvalidShotDoesNotMutateExistingFeedback)
{
    CombatFeedbackState feedback;
    ASSERT_TRUE(feedback.recordShot({1.0F, 2.0F}, {1.0F, 0.0F}));

    EXPECT_FALSE(feedback.recordShot({9.0F, 9.0F}, {0.0F, 0.0F}));
    EXPECT_FALSE(feedback.recordShot(
        {9.0F, 9.0F},
        {std::numeric_limits<float>::max(),
         std::numeric_limits<float>::max()}));
    EXPECT_FALSE(feedback.recordShot(
        {std::numeric_limits<float>::quiet_NaN(), 9.0F},
        {1.0F, 0.0F}));

    EXPECT_FLOAT_EQ(feedback.muzzleFlashIntensity(), 1.0F);
    EXPECT_FLOAT_EQ(feedback.muzzleOrigin().x, 1.0F);
    EXPECT_FLOAT_EQ(feedback.muzzleOrigin().y, 2.0F);
}

TEST(CombatFeedbackTest, TimersDecayAndClampAtZero)
{
    CombatFeedbackState feedback;
    ASSERT_TRUE(feedback.recordShot({1.0F, 2.0F}, {1.0F, 0.0F}));
    feedback.recordEnemyHit();
    ASSERT_TRUE(feedback.recordPlayerHit(EnemyAttackType::Scratch));

    feedback.update(0.03F);
    EXPECT_GT(feedback.muzzleFlashIntensity(), 0.0F);
    EXPECT_LT(feedback.muzzleFlashIntensity(), 1.0F);
    EXPECT_GT(feedback.hitConfirmIntensity(), 0.0F);
    EXPECT_GT(feedback.playerDamagePulseIntensity(), 0.0F);

    feedback.update(10.0F);
    EXPECT_FLOAT_EQ(feedback.muzzleFlashIntensity(), 0.0F);
    EXPECT_FLOAT_EQ(feedback.hitConfirmIntensity(), 0.0F);
    EXPECT_FLOAT_EQ(feedback.playerDamagePulseIntensity(), 0.0F);
    EXPECT_FLOAT_EQ(feedback.muzzleOrigin().x, 0.0F);
    EXPECT_FLOAT_EQ(feedback.muzzleDirection().x, 0.0F);
}

TEST(CombatFeedbackTest, EnemyHitRefreshesHitConfirm)
{
    CombatFeedbackState feedback;
    feedback.recordEnemyHit();
    feedback.update(0.06F);
    ASSERT_GT(feedback.hitConfirmIntensity(), 0.0F);
    ASSERT_LT(feedback.hitConfirmIntensity(), 1.0F);

    feedback.recordEnemyHit();
    EXPECT_FLOAT_EQ(feedback.hitConfirmIntensity(), 1.0F);
}

TEST(CombatFeedbackTest, ScratchAndBiteUseBoundedDifferentStrengths)
{
    CombatFeedbackState feedback;

    ASSERT_TRUE(feedback.recordPlayerHit(EnemyAttackType::Scratch));
    EXPECT_FLOAT_EQ(feedback.playerDamagePulseIntensity(), 0.55F);

    feedback.reset();
    ASSERT_TRUE(feedback.recordPlayerHit(EnemyAttackType::Bite));
    EXPECT_FLOAT_EQ(feedback.playerDamagePulseIntensity(), 1.0F);
}

TEST(CombatFeedbackTest, GrabAndInvalidAttackTypesAreRejected)
{
    CombatFeedbackState feedback;

    EXPECT_FALSE(feedback.recordPlayerHit(EnemyAttackType::Grab));
    EXPECT_FALSE(feedback.recordPlayerHit(static_cast<EnemyAttackType>(99)));
    EXPECT_FLOAT_EQ(feedback.playerDamagePulseIntensity(), 0.0F);
}

TEST(CombatFeedbackTest, RepeatedPlayerHitsRefreshWithoutAccumulatingPastOne)
{
    CombatFeedbackState feedback;
    ASSERT_TRUE(feedback.recordPlayerHit(EnemyAttackType::Bite));
    feedback.update(0.09F);
    ASSERT_FLOAT_EQ(feedback.playerDamagePulseIntensity(), 0.5F);

    ASSERT_TRUE(feedback.recordPlayerHit(EnemyAttackType::Scratch));
    EXPECT_FLOAT_EQ(feedback.playerDamagePulseIntensity(), 1.0F);
    ASSERT_TRUE(feedback.recordPlayerHit(EnemyAttackType::Bite));
    EXPECT_FLOAT_EQ(feedback.playerDamagePulseIntensity(), 1.0F);
}

TEST(CombatFeedbackTest, InvalidDeltaTimeLeavesStateUnchanged)
{
    CombatFeedbackState feedback;
    feedback.recordEnemyHit();

    feedback.update(-1.0F);
    feedback.update(std::numeric_limits<float>::infinity());
    feedback.update(std::numeric_limits<float>::quiet_NaN());

    EXPECT_FLOAT_EQ(feedback.hitConfirmIntensity(), 1.0F);
}

TEST(CombatFeedbackTest, InvalidConfigurationsAreRejected)
{
    CombatFeedbackConfig config{};
    config.muzzleFlashDuration = 0.0F;
    EXPECT_THROW((CombatFeedbackState{config}), std::invalid_argument);

    config = CombatFeedbackConfig{};
    config.scratchPulseStrength = 1.1F;
    EXPECT_THROW((CombatFeedbackState{config}), std::invalid_argument);

    config = CombatFeedbackConfig{};
    config.scratchPulseStrength = 0.9F;
    config.bitePulseStrength = 0.8F;
    EXPECT_THROW((CombatFeedbackState{config}), std::invalid_argument);
}

TEST(CombatFeedbackTest, ResetClearsEveryChannel)
{
    CombatFeedbackState feedback;
    ASSERT_TRUE(feedback.recordShot({1.0F, 2.0F}, {1.0F, 1.0F}));
    feedback.recordEnemyHit();
    ASSERT_TRUE(feedback.recordPlayerHit(EnemyAttackType::Bite));

    feedback.reset();

    EXPECT_FLOAT_EQ(feedback.muzzleFlashIntensity(), 0.0F);
    EXPECT_FLOAT_EQ(feedback.hitConfirmIntensity(), 0.0F);
    EXPECT_FLOAT_EQ(feedback.playerDamagePulseIntensity(), 0.0F);
    EXPECT_FLOAT_EQ(feedback.muzzleOrigin().x, 0.0F);
    EXPECT_FLOAT_EQ(feedback.muzzleDirection().x, 0.0F);
}
