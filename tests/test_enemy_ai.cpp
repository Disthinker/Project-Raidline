#include <gtest/gtest.h>

#include <limits>
#include <stdexcept>

#include "enemy_ai.h"

namespace
{
    constexpr float kTolerance{0.0001F};
}

TEST(EnemyAiStateTest, FarTargetProducesNormalizedPursuit)
{
    EnemyAiState ai;

    const EnemyAiDecision decision =
        ai.update(Vec2{300.0F, 400.0F}, true, 0.0F);

    EXPECT_FALSE(decision.attackRequest.has_value());
    EXPECT_NEAR(decision.moveDirection.x, 0.6F, kTolerance);
    EXPECT_NEAR(decision.moveDirection.y, 0.8F, kTolerance);
}

TEST(EnemyAiStateTest, FirstApproachNeverOpensWithGrab)
{
    EnemyAiState ai;

    const EnemyAiDecision decision =
        ai.update(Vec2{120.0F, 0.0F}, true, 1.0F);

    EXPECT_FALSE(decision.attackRequest.has_value());
    EXPECT_FLOAT_EQ(decision.moveDirection.x, 1.0F);
    EXPECT_FALSE(ai.specialChargeArmed());
}

TEST(EnemyAiStateTest, CloseRangeDefaultsToScratchAndArmsSpecialCharge)
{
    EnemyAiState ai;

    const EnemyAiDecision decision =
        ai.update(Vec2{70.0F, 0.0F}, true, 0.0F);
    ASSERT_EQ(decision.attackRequest, EnemyAttackType::Scratch);
    ai.recordAttackStarted(*decision.attackRequest);

    EXPECT_TRUE(ai.specialChargeArmed());
    EXPECT_FLOAT_EQ(
        ai.specialChargeHoldRemaining(),
        ai.config().specialChargeHoldDuration);
}

TEST(EnemyAiStateTest, SpecialChargeRequiresArmedMidRangeHold)
{
    EnemyAiState ai;
    ai.recordAttackStarted(EnemyAttackType::Scratch);

    EnemyAiDecision decision =
        ai.update(Vec2{130.0F, 0.0F}, true, 0.49F);
    EXPECT_FALSE(decision.attackRequest.has_value());
    EXPECT_FLOAT_EQ(decision.moveDirection.x, 1.0F);

    decision = ai.update(Vec2{130.0F, 0.0F}, true, 0.01F);
    ASSERT_EQ(decision.attackRequest, EnemyAttackType::Grab);
    EXPECT_FLOAT_EQ(decision.moveDirection.x, 0.0F);
}

TEST(EnemyAiStateTest, LeavingSpecialBandResetsHoldProgress)
{
    EnemyAiState ai;
    ai.recordAttackStarted(EnemyAttackType::Scratch);

    static_cast<void>(
        ai.update(Vec2{130.0F, 0.0F}, true, 0.30F));
    static_cast<void>(
        ai.update(Vec2{200.0F, 0.0F}, true, 0.01F));
    const EnemyAiDecision decision =
        ai.update(Vec2{130.0F, 0.0F}, true, 0.30F);

    EXPECT_FALSE(decision.attackRequest.has_value());
    EXPECT_NEAR(
        ai.specialChargeHoldRemaining(),
        0.20F,
        kTolerance);
}

TEST(EnemyAiStateTest, StartingGrabConsumesArmAndStartsLongCooldown)
{
    EnemyAiState ai;
    ai.recordAttackStarted(EnemyAttackType::Scratch);
    ai.recordAttackStarted(EnemyAttackType::Grab);

    EXPECT_FALSE(ai.specialChargeArmed());
    EXPECT_FLOAT_EQ(
        ai.cooldownRemaining(EnemyAttackType::Grab),
        4.0F);
    EXPECT_FLOAT_EQ(
        ai.specialChargeHoldRemaining(),
        ai.config().specialChargeHoldDuration);
}

TEST(EnemyAiStateTest, BiteIsNeverSelectedAsStandaloneCloseAttack)
{
    EnemyAiState ai;
    ai.recordAttackStarted(EnemyAttackType::Scratch);
    static_cast<void>(
        ai.update(Vec2{70.0F, 0.0F}, false, 0.65F));

    const EnemyAiDecision decision =
        ai.update(Vec2{70.0F, 0.0F}, true, 0.0F);

    ASSERT_EQ(decision.attackRequest, EnemyAttackType::Scratch);
}

TEST(EnemyAiStateTest, CloseTargetStopsWhileScratchCoolsDown)
{
    EnemyAiState ai;
    ai.recordAttackStarted(EnemyAttackType::Scratch);

    const EnemyAiDecision decision =
        ai.update(Vec2{40.0F, 0.0F}, true, 0.0F);

    EXPECT_FALSE(decision.attackRequest.has_value());
    EXPECT_FLOAT_EQ(decision.moveDirection.x, 0.0F);
    EXPECT_FLOAT_EQ(decision.moveDirection.y, 0.0F);
}

TEST(EnemyAiStateTest, ActiveAttackSuppressesMovementAndNewSelection)
{
    EnemyAiState ai;
    ai.recordAttackStarted(EnemyAttackType::Grab);

    const EnemyAiDecision decision =
        ai.update(Vec2{300.0F, 0.0F}, false, 0.5F);

    EXPECT_FALSE(decision.attackRequest.has_value());
    EXPECT_FLOAT_EQ(decision.moveDirection.x, 0.0F);
    EXPECT_NEAR(
        ai.cooldownRemaining(EnemyAttackType::Grab),
        3.5F,
        kTolerance);
}

TEST(EnemyAiStateTest, InvalidOrZeroTargetProducesNoIntent)
{
    EnemyAiState ai;

    EnemyAiDecision decision =
        ai.update(Vec2{}, true, 0.0F);
    EXPECT_FALSE(decision.attackRequest.has_value());
    EXPECT_FLOAT_EQ(decision.moveDirection.x, 0.0F);

    decision = ai.update(
        Vec2{
            std::numeric_limits<float>::quiet_NaN(),
            1.0F},
        true,
        0.0F);
    EXPECT_FALSE(decision.attackRequest.has_value());
    EXPECT_FLOAT_EQ(decision.moveDirection.x, 0.0F);
}

TEST(EnemyAiStateTest, InvalidThresholdOrSpeedOrderingIsRejected)
{
    EnemyAiConfig config;
    config.stopDistance = config.scratchAttackDistance;
    EXPECT_THROW(
        static_cast<void>(EnemyAiState{config}),
        std::invalid_argument);

    config = EnemyAiConfig{};
    config.normalMoveSpeed = config.attackMoveSpeed;
    EXPECT_THROW(
        static_cast<void>(EnemyAiState{config}),
        std::invalid_argument);
}
