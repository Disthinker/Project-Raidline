#include <gtest/gtest.h>

#include <limits>
#include <optional>

#include "enemy_attack_presentation.h"

namespace
{
    EnemyAttackConfig configFor(EnemyAttackType type)
    {
        const EnemyAttackConfigSet configs =
            defaultEnemyAttackConfigs();
        switch (type)
        {
        case EnemyAttackType::Grab:
            return configs[0];
        case EnemyAttackType::Scratch:
            return configs[1];
        case EnemyAttackType::Bite:
            return configs[2];
        }

        return configs[0];
    }
}

TEST(EnemyAttackPresentationTest, IdleAndOffBalanceUseMovementFallback)
{
    const EnemyAttackConfig config =
        configFor(EnemyAttackType::Grab);

    EXPECT_FALSE(sampleEnemyAttackPresentation(
                     EnemyAttackType::Grab,
                     EnemyAttackPhase::Idle,
                     0.0F,
                     config)
                     .usesAttackSheet);
    EXPECT_FALSE(sampleEnemyAttackPresentation(
                     EnemyAttackType::Grab,
                     EnemyAttackPhase::OffBalance,
                     config.missRecoveryDuration,
                     config)
                     .usesAttackSheet);
}

TEST(EnemyAttackPresentationTest, MissingSnapshotDataUsesMovementFallback)
{
    const EnemyAttackConfig config =
        configFor(EnemyAttackType::Scratch);

    EXPECT_FALSE(sampleEnemyAttackPresentation(
                     std::nullopt,
                     EnemyAttackPhase::Windup,
                     config.windupDuration,
                     config)
                     .usesAttackSheet);
    EXPECT_FALSE(sampleEnemyAttackPresentation(
                     EnemyAttackType::Scratch,
                     EnemyAttackPhase::Windup,
                     config.windupDuration,
                     std::nullopt)
                     .usesAttackSheet);
}

TEST(EnemyAttackPresentationTest, ScratchMapsWindupActiveAndRecoveryToSixFrames)
{
    const EnemyAttackConfig config =
        configFor(EnemyAttackType::Scratch);

    EXPECT_EQ(sampleEnemyAttackPresentation(
                  EnemyAttackType::Scratch,
                  EnemyAttackPhase::Windup,
                  config.windupDuration,
                  config)
                  .frameIndex,
              0U);
    EXPECT_EQ(sampleEnemyAttackPresentation(
                  EnemyAttackType::Scratch,
                  EnemyAttackPhase::Windup,
                  config.windupDuration * 0.49F,
                  config)
                  .frameIndex,
              1U);
    EXPECT_EQ(sampleEnemyAttackPresentation(
                  EnemyAttackType::Scratch,
                  EnemyAttackPhase::Active,
                  config.activeDuration,
                  config)
                  .frameIndex,
              2U);
    EXPECT_EQ(sampleEnemyAttackPresentation(
                  EnemyAttackType::Scratch,
                  EnemyAttackPhase::Active,
                  config.activeDuration * 0.60F,
                  config)
                  .frameIndex,
              3U);
    EXPECT_EQ(sampleEnemyAttackPresentation(
                  EnemyAttackType::Scratch,
                  EnemyAttackPhase::Active,
                  0.0F,
                  config)
                  .frameIndex,
              4U);
    EXPECT_EQ(sampleEnemyAttackPresentation(
                  EnemyAttackType::Scratch,
                  EnemyAttackPhase::Recovery,
                  config.recoveryDuration,
                  config)
                  .frameIndex,
              5U);
}

TEST(EnemyAttackPresentationTest, GrabUsesThreeWindupAndTwoActiveFrames)
{
    const EnemyAttackConfig config =
        configFor(EnemyAttackType::Grab);

    EXPECT_EQ(sampleEnemyAttackPresentation(
                  EnemyAttackType::Grab,
                  EnemyAttackPhase::Windup,
                  config.windupDuration,
                  config)
                  .frameIndex,
              0U);
    EXPECT_EQ(sampleEnemyAttackPresentation(
                  EnemyAttackType::Grab,
                  EnemyAttackPhase::Windup,
                  config.windupDuration * 0.60F,
                  config)
                  .frameIndex,
              1U);
    EXPECT_EQ(sampleEnemyAttackPresentation(
                  EnemyAttackType::Grab,
                  EnemyAttackPhase::Windup,
                  config.windupDuration * 0.20F,
                  config)
                  .frameIndex,
              2U);
    EXPECT_EQ(sampleEnemyAttackPresentation(
                  EnemyAttackType::Grab,
                  EnemyAttackPhase::Active,
                  config.activeDuration,
                  config)
                  .frameIndex,
              3U);
    EXPECT_EQ(sampleEnemyAttackPresentation(
                  EnemyAttackType::Grab,
                  EnemyAttackPhase::Active,
                  0.0F,
                  config)
                  .frameIndex,
              4U);
}

TEST(EnemyAttackPresentationTest, BiteUsesScratchFramePartition)
{
    const EnemyAttackConfig config =
        configFor(EnemyAttackType::Bite);

    const auto activeStart = sampleEnemyAttackPresentation(
        EnemyAttackType::Bite,
        EnemyAttackPhase::Active,
        config.activeDuration,
        config);
    const auto activeEnd = sampleEnemyAttackPresentation(
        EnemyAttackType::Bite,
        EnemyAttackPhase::Active,
        0.0F,
        config);

    EXPECT_TRUE(activeStart.usesAttackSheet);
    EXPECT_EQ(activeStart.frameIndex, 2U);
    EXPECT_EQ(activeEnd.frameIndex, 4U);
    EXPECT_FLOAT_EQ(activeStart.emphasis, 1.0F);
}

TEST(EnemyAttackPresentationTest, ProgressAndEmphasisStayBounded)
{
    const EnemyAttackConfig config =
        configFor(EnemyAttackType::Scratch);

    const auto beforeStart = sampleEnemyAttackPresentation(
        EnemyAttackType::Scratch,
        EnemyAttackPhase::Windup,
        config.windupDuration * 2.0F,
        config);
    const auto afterEnd = sampleEnemyAttackPresentation(
        EnemyAttackType::Scratch,
        EnemyAttackPhase::Recovery,
        0.0F,
        config);

    EXPECT_FLOAT_EQ(beforeStart.phaseProgress, 0.0F);
    EXPECT_GE(beforeStart.emphasis, 0.0F);
    EXPECT_LE(beforeStart.emphasis, 1.0F);
    EXPECT_FLOAT_EQ(afterEnd.phaseProgress, 1.0F);
    EXPECT_FLOAT_EQ(afterEnd.emphasis, 0.0F);
}

TEST(EnemyAttackPresentationTest, InvalidNumericOrEnumInputUsesFallback)
{
    EnemyAttackConfig config =
        configFor(EnemyAttackType::Scratch);

    EXPECT_FALSE(sampleEnemyAttackPresentation(
                     EnemyAttackType::Scratch,
                     EnemyAttackPhase::Windup,
                     -0.1F,
                     config)
                     .usesAttackSheet);
    EXPECT_FALSE(sampleEnemyAttackPresentation(
                     EnemyAttackType::Scratch,
                     EnemyAttackPhase::Windup,
                     std::numeric_limits<float>::quiet_NaN(),
                     config)
                     .usesAttackSheet);

    config.windupDuration = 0.0F;
    EXPECT_FALSE(sampleEnemyAttackPresentation(
                     EnemyAttackType::Scratch,
                     EnemyAttackPhase::Windup,
                     0.0F,
                     config)
                     .usesAttackSheet);
    EXPECT_FALSE(sampleEnemyAttackPresentation(
                     static_cast<EnemyAttackType>(99),
                     EnemyAttackPhase::Windup,
                     0.1F,
                     configFor(EnemyAttackType::Scratch))
                     .usesAttackSheet);
    EXPECT_FALSE(sampleEnemyAttackPresentation(
                     EnemyAttackType::Scratch,
                     static_cast<EnemyAttackPhase>(99),
                     0.1F,
                     configFor(EnemyAttackType::Scratch))
                     .usesAttackSheet);
}

TEST(EnemyAttackPresentationTest, SameSnapshotAlwaysProducesSameSample)
{
    const EnemyAttackConfig config =
        configFor(EnemyAttackType::Bite);

    const auto first = sampleEnemyAttackPresentation(
        EnemyAttackType::Bite,
        EnemyAttackPhase::Recovery,
        config.recoveryDuration * 0.4F,
        config);
    const auto second = sampleEnemyAttackPresentation(
        EnemyAttackType::Bite,
        EnemyAttackPhase::Recovery,
        config.recoveryDuration * 0.4F,
        config);

    EXPECT_EQ(first.usesAttackSheet, second.usesAttackSheet);
    EXPECT_EQ(first.frameIndex, second.frameIndex);
    EXPECT_FLOAT_EQ(first.phaseProgress, second.phaseProgress);
    EXPECT_FLOAT_EQ(first.emphasis, second.emphasis);
}
