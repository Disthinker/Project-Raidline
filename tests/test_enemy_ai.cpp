#include <gtest/gtest.h>

#include <limits>
#include <stdexcept>

#include "enemy_ai.h"

namespace
{
    constexpr float kTolerance{0.0001F};

    EnemyAiInput makeInput(
        Vec2 targetPosition,
        float deltaTime = 0.0F,
        EnemyTacticalRole role = EnemyTacticalRole::Engage,
        bool canStartAttack = true,
        Vec2 selfPosition = Vec2{})
    {
        return EnemyAiInput{
            selfPosition,
            targetPosition,
            EnemyTacticalDirective{
                role,
                canStartAttack,
                Vec2{},
                1.0F},
            deltaTime};
    }
}

TEST(EnemyAiStateTest, TargetOutsideAcquireDistanceKeepsEnemyUnaware)
{
    EnemyAiState ai;

    const EnemyAiDecision decision =
        ai.update(makeInput(Vec2{500.0F, 0.0F}, 1.0F));

    EXPECT_EQ(ai.awarenessState(), EnemyAwarenessState::Unaware);
    EXPECT_FALSE(ai.lastKnownTargetPosition().has_value());
    EXPECT_FALSE(decision.attackRequest.has_value());
    EXPECT_FLOAT_EQ(decision.moveDirection.x, 0.0F);
}

TEST(EnemyAiStateTest, NearbyTargetIsAcquiredAndPursued)
{
    EnemyAiState ai;

    const EnemyAiDecision decision =
        ai.update(makeInput(Vec2{180.0F, 240.0F}));

    EXPECT_EQ(ai.awarenessState(), EnemyAwarenessState::Alerted);
    ASSERT_TRUE(ai.lastKnownTargetPosition().has_value());
    EXPECT_FLOAT_EQ(ai.lastKnownTargetPosition()->x, 180.0F);
    EXPECT_NEAR(decision.moveDirection.x, 0.6F, kTolerance);
    EXPECT_NEAR(decision.moveDirection.y, 0.8F, kTolerance);
}

TEST(EnemyAiStateTest, HiddenNearbyTargetIsNotAcquired)
{
    EnemyAiState ai;
    EnemyAiInput input = makeInput(Vec2{120.0F, 0.0F});
    input.targetVisible = false;

    const EnemyAiDecision decision = ai.update(input);

    EXPECT_EQ(ai.awarenessState(), EnemyAwarenessState::Unaware);
    EXPECT_FALSE(ai.lastKnownTargetPosition().has_value());
    EXPECT_FALSE(decision.attackRequest.has_value());
    EXPECT_FLOAT_EQ(decision.moveDirection.x, 0.0F);
}

TEST(EnemyAiStateTest, LosingLineOfSightSearchesFrozenLastKnownPosition)
{
    EnemyAiState ai;
    static_cast<void>(ai.update(makeInput(Vec2{200.0F, 0.0F})));

    EnemyAiInput hidden = makeInput(Vec2{240.0F, 80.0F}, 0.1F);
    hidden.targetVisible = false;
    hidden.navigationTarget = Vec2{100.0F, -80.0F};
    const EnemyAiDecision decision = ai.update(hidden);

    ASSERT_EQ(ai.awarenessState(), EnemyAwarenessState::Searching);
    ASSERT_TRUE(ai.lastKnownTargetPosition().has_value());
    EXPECT_FLOAT_EQ(ai.lastKnownTargetPosition()->x, 200.0F);
    EXPECT_FLOAT_EQ(ai.lastKnownTargetPosition()->y, 0.0F);
    EXPECT_GT(decision.moveDirection.x, 0.0F);
    EXPECT_LT(decision.moveDirection.y, 0.0F);
}

TEST(EnemyAiStateTest, AudibleHiddenTargetInvestigatesWithoutAttacking)
{
    EnemyAiState ai;
    ai.hearTarget(Vec2{70.0F, 0.0F});
    EnemyAiInput input = makeInput(Vec2{70.0F, 0.0F});
    input.targetVisible = false;

    const EnemyAiDecision decision = ai.update(input);

    EXPECT_EQ(ai.awarenessState(), EnemyAwarenessState::Searching);
    EXPECT_FALSE(decision.attackRequest.has_value());
    EXPECT_GT(decision.moveDirection.x, 0.0F);
}

TEST(EnemyAiStateTest, AcquireAndLoseDistancesProvideHysteresis)
{
    EnemyAiState ai;
    static_cast<void>(ai.update(makeInput(Vec2{300.0F, 0.0F})));

    static_cast<void>(ai.update(makeInput(Vec2{420.0F, 0.0F})));
    EXPECT_EQ(ai.awarenessState(), EnemyAwarenessState::Alerted);

    static_cast<void>(ai.update(makeInput(Vec2{500.0F, 0.0F})));
    EXPECT_EQ(ai.awarenessState(), EnemyAwarenessState::Searching);
}

TEST(EnemyAiStateTest, SearchingUsesFrozenLastKnownPosition)
{
    EnemyAiState ai;
    static_cast<void>(ai.update(makeInput(Vec2{300.0F, 20.0F})));
    static_cast<void>(ai.update(makeInput(Vec2{520.0F, 200.0F}, 0.1F)));

    ASSERT_EQ(ai.awarenessState(), EnemyAwarenessState::Searching);
    ASSERT_TRUE(ai.lastKnownTargetPosition().has_value());
    EXPECT_FLOAT_EQ(ai.lastKnownTargetPosition()->x, 300.0F);
    EXPECT_FLOAT_EQ(ai.lastKnownTargetPosition()->y, 20.0F);

    static_cast<void>(ai.update(makeInput(Vec2{700.0F, -200.0F}, 0.1F)));
    ASSERT_TRUE(ai.lastKnownTargetPosition().has_value());
    EXPECT_FLOAT_EQ(ai.lastKnownTargetPosition()->x, 300.0F);
    EXPECT_FLOAT_EQ(ai.lastKnownTargetPosition()->y, 20.0F);
}

TEST(EnemyAiStateTest, SearchStopsWhenMemoryExpires)
{
    EnemyAiState ai;
    static_cast<void>(ai.update(makeInput(Vec2{300.0F, 0.0F})));
    static_cast<void>(ai.update(makeInput(Vec2{600.0F, 0.0F}, 0.5F)));

    const EnemyAiDecision decision =
        ai.update(makeInput(
            Vec2{600.0F, 0.0F},
            ai.config().searchMemoryDuration - 0.5F));

    EXPECT_EQ(ai.awarenessState(), EnemyAwarenessState::Unaware);
    EXPECT_FALSE(ai.lastKnownTargetPosition().has_value());
    EXPECT_FLOAT_EQ(decision.moveDirection.x, 0.0F);
}

TEST(EnemyAiStateTest, SearchStopsOnArrivalAtLastKnownPosition)
{
    EnemyAiState ai;
    static_cast<void>(ai.update(makeInput(Vec2{300.0F, 0.0F})));
    static_cast<void>(ai.update(makeInput(Vec2{600.0F, 0.0F}, 0.1F)));

    const EnemyAiDecision decision = ai.update(
        makeInput(
            Vec2{800.0F, 0.0F},
            0.1F,
            EnemyTacticalRole::Support,
            false,
            Vec2{290.0F, 0.0F}));

    EXPECT_EQ(ai.awarenessState(), EnemyAwarenessState::Unaware);
    EXPECT_FALSE(ai.lastKnownTargetPosition().has_value());
    EXPECT_FLOAT_EQ(decision.moveDirection.x, 0.0F);
}

TEST(EnemyAiStateTest, SearchingEnemyCanReacquireNearbyTarget)
{
    EnemyAiState ai;
    static_cast<void>(ai.update(makeInput(Vec2{300.0F, 0.0F})));
    static_cast<void>(ai.update(makeInput(Vec2{600.0F, 0.0F}, 0.1F)));

    const EnemyAiDecision decision =
        ai.update(makeInput(Vec2{200.0F, 0.0F}, 0.1F));

    EXPECT_EQ(ai.awarenessState(), EnemyAwarenessState::Alerted);
    EXPECT_GT(decision.moveDirection.x, 0.0F);
}

TEST(EnemyAiStateTest, FirstApproachNeverOpensWithGrab)
{
    EnemyAiState ai;

    const EnemyAiDecision decision =
        ai.update(makeInput(Vec2{120.0F, 0.0F}, 1.0F));

    EXPECT_FALSE(decision.attackRequest.has_value());
    EXPECT_FLOAT_EQ(decision.moveDirection.x, 1.0F);
    EXPECT_FALSE(ai.specialChargeArmed());
}

TEST(EnemyAiStateTest, CloseRangeDefaultsToScratchAndArmsSpecialCharge)
{
    EnemyAiState ai;

    const EnemyAiDecision decision =
        ai.update(makeInput(Vec2{70.0F, 0.0F}));
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
    static_cast<void>(ai.update(makeInput(Vec2{70.0F, 0.0F})));
    ai.recordAttackStarted(EnemyAttackType::Scratch);

    EnemyAiDecision decision =
        ai.update(makeInput(Vec2{130.0F, 0.0F}, 0.49F));
    EXPECT_FALSE(decision.attackRequest.has_value());

    decision = ai.update(makeInput(Vec2{130.0F, 0.0F}, 0.01F));
    ASSERT_EQ(decision.attackRequest, EnemyAttackType::Grab);
    EXPECT_FLOAT_EQ(decision.moveDirection.x, 0.0F);
}

TEST(EnemyAiStateTest, LeavingSpecialBandResetsHoldProgress)
{
    EnemyAiState ai;
    static_cast<void>(ai.update(makeInput(Vec2{70.0F, 0.0F})));
    ai.recordAttackStarted(EnemyAttackType::Scratch);

    static_cast<void>(
        ai.update(makeInput(Vec2{130.0F, 0.0F}, 0.30F)));
    static_cast<void>(
        ai.update(makeInput(Vec2{200.0F, 0.0F}, 0.01F)));
    const EnemyAiDecision decision =
        ai.update(makeInput(Vec2{130.0F, 0.0F}, 0.30F));

    EXPECT_FALSE(decision.attackRequest.has_value());
    EXPECT_NEAR(
        ai.specialChargeHoldRemaining(),
        0.20F,
        kTolerance);
}

TEST(EnemyAiStateTest, CloseTargetStopsWhileScratchCoolsDown)
{
    EnemyAiState ai;
    static_cast<void>(ai.update(makeInput(Vec2{70.0F, 0.0F})));
    ai.recordAttackStarted(EnemyAttackType::Scratch);

    const EnemyAiDecision decision =
        ai.update(makeInput(Vec2{40.0F, 0.0F}));

    EXPECT_FALSE(decision.attackRequest.has_value());
    EXPECT_FLOAT_EQ(decision.moveDirection.x, 0.0F);
    EXPECT_FLOAT_EQ(decision.moveDirection.y, 0.0F);
}

TEST(EnemyAiStateTest, BiteIsNeverSelectedAsStandaloneAttack)
{
    EnemyAiState ai;
    static_cast<void>(ai.update(makeInput(Vec2{70.0F, 0.0F})));
    ai.recordAttackStarted(EnemyAttackType::Scratch);
    static_cast<void>(ai.update(
        makeInput(
            Vec2{70.0F, 0.0F},
            0.65F,
            EnemyTacticalRole::Support,
            false)));

    const EnemyAiDecision decision =
        ai.update(makeInput(Vec2{70.0F, 0.0F}));

    ASSERT_TRUE(decision.attackRequest.has_value());
    EXPECT_EQ(*decision.attackRequest, EnemyAttackType::Scratch);
}

TEST(EnemyAiStateTest, SupportRoleCannotAttackAndRetreatsWhenTooClose)
{
    EnemyAiState ai;

    const EnemyAiDecision decision = ai.update(
        makeInput(
            Vec2{70.0F, 0.0F},
            0.0F,
            EnemyTacticalRole::Support,
            false));

    EXPECT_FALSE(decision.attackRequest.has_value());
    EXPECT_LT(decision.moveDirection.x, 0.0F);
}

TEST(EnemyAiStateTest, SupportRoleOrbitsInsideItsDistanceBand)
{
    EnemyAiState ai;

    const EnemyAiDecision decision = ai.update(
        makeInput(
            Vec2{130.0F, 0.0F},
            0.0F,
            EnemyTacticalRole::Support,
            false));

    EXPECT_NEAR(decision.moveDirection.x, 0.0F, kTolerance);
    EXPECT_NEAR(decision.moveDirection.y, 1.0F, kTolerance);
}

TEST(EnemyAiStateTest, TurningTowardOppositeTargetIsRateLimited)
{
    EnemyAiState ai;
    static_cast<void>(ai.update(makeInput(Vec2{200.0F, 0.0F})));

    const EnemyAiDecision decision =
        ai.update(makeInput(Vec2{-200.0F, 0.0F}, 0.1F));

    EXPECT_GT(decision.moveDirection.x, 0.0F);
    EXPECT_GT(decision.moveDirection.y, 0.0F);
    EXPECT_NEAR(
        decision.moveDirection.x * decision.moveDirection.x +
            decision.moveDirection.y * decision.moveDirection.y,
        1.0F,
        kTolerance);
}

TEST(EnemyAiStateTest, SeparationBiasChangesPursuitDirection)
{
    EnemyAiState ai;
    EnemyAiInput input = makeInput(Vec2{200.0F, 0.0F});
    input.tactical.separationDirection = Vec2{0.0F, 1.0F};

    const EnemyAiDecision decision = ai.update(input);

    EXPECT_GT(decision.moveDirection.x, 0.0F);
    EXPECT_GT(decision.moveDirection.y, 0.0F);
}

TEST(EnemyAiStateTest, InvalidTargetDoesNotPollutePerception)
{
    EnemyAiState ai;
    EnemyAiInput input = makeInput(Vec2{});
    input.targetPosition.x =
        std::numeric_limits<float>::quiet_NaN();

    const EnemyAiDecision decision = ai.update(input);

    EXPECT_EQ(ai.awarenessState(), EnemyAwarenessState::Unaware);
    EXPECT_FALSE(ai.lastKnownTargetPosition().has_value());
    EXPECT_FALSE(decision.attackRequest.has_value());

    input = makeInput(Vec2{
        std::numeric_limits<float>::max(),
        std::numeric_limits<float>::max()});
    const EnemyAiDecision overflowDecision = ai.update(input);
    EXPECT_EQ(ai.awarenessState(), EnemyAwarenessState::Unaware);
    EXPECT_FLOAT_EQ(overflowDecision.moveDirection.x, 0.0F);
}

TEST(EnemyAiStateTest, TurnRateIsStableAcrossEquivalentFramePartitions)
{
    EnemyAiState oneStep;
    EnemyAiState twoSteps;
    static_cast<void>(oneStep.update(makeInput(Vec2{200.0F, 0.0F})));
    static_cast<void>(twoSteps.update(makeInput(Vec2{200.0F, 0.0F})));

    const EnemyAiDecision oneStepDecision =
        oneStep.update(makeInput(Vec2{-200.0F, 0.0F}, 0.2F));
    static_cast<void>(
        twoSteps.update(makeInput(Vec2{-200.0F, 0.0F}, 0.1F)));
    const EnemyAiDecision twoStepDecision =
        twoSteps.update(makeInput(Vec2{-200.0F, 0.0F}, 0.1F));

    EXPECT_NEAR(
        oneStepDecision.moveDirection.x,
        twoStepDecision.moveDirection.x,
        kTolerance);
    EXPECT_NEAR(
        oneStepDecision.moveDirection.y,
        twoStepDecision.moveDirection.y,
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
}

TEST(EnemyAiStateTest, AudibleTargetImmediatelyCreatesAlertMemory)
{
    EnemyAiState ai;
    ai.hearTarget(Vec2{120.0F, 80.0F});

    EXPECT_EQ(ai.awarenessState(), EnemyAwarenessState::Alerted);
    ASSERT_TRUE(ai.lastKnownTargetPosition().has_value());
    EXPECT_FLOAT_EQ(ai.lastKnownTargetPosition()->x, 120.0F);
    EXPECT_FLOAT_EQ(ai.searchTimeRemaining(), ai.config().searchMemoryDuration);
}

TEST(EnemyAiStateTest, InvalidThresholdOrSpeedOrderingIsRejected)
{
    EnemyAiConfig config;
    config.acquireTargetDistance = config.loseTargetDistance;
    EXPECT_THROW(
        static_cast<void>(EnemyAiState{config}),
        std::invalid_argument);

    config = EnemyAiConfig{};
    config.supportMinDistance = config.scratchAttackDistance;
    EXPECT_THROW(
        static_cast<void>(EnemyAiState{config}),
        std::invalid_argument);
}
