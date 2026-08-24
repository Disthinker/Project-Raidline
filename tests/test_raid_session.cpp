#include <gtest/gtest.h>

#include <limits>
#include <stdexcept>

#include "raid_session.h"

namespace
{
RaidSessionConfig highRiskConfig(
    float regularDuration = 5.0F,
    float normalExtractionDuration = 3.0F,
    float emergencyExtractionDuration = 4.0F)
{
    return RaidSessionConfig{
        0.0F,
        normalExtractionDuration,
        false,
        HighRiskRaidSessionConfig{
            true,
            regularDuration,
            emergencyExtractionDuration,
            2.0F}};
}
}

TEST(RaidSessionTest, RejectsInvalidDurations)
{
    EXPECT_THROW(
        (void)RaidSession(RaidSessionConfig{0.0F, 3.0F}),
        std::invalid_argument);
    EXPECT_THROW(
        (void)RaidSession(RaidSessionConfig{180.0F, -1.0F}),
        std::invalid_argument);
    EXPECT_THROW(
        (void)RaidSession(RaidSessionConfig{
            std::numeric_limits<float>::infinity(),
            3.0F}),
        std::invalid_argument);
    EXPECT_THROW(
        (void)RaidSession(RaidSessionConfig{
            180.0F,
            std::numeric_limits<float>::quiet_NaN()}),
        std::invalid_argument);
    EXPECT_THROW(
        (void)RaidSession(RaidSessionConfig{
            180.0F,
            3.0F,
            true,
            HighRiskRaidSessionConfig{true, 5.0F, 4.0F}}),
        std::invalid_argument);
    EXPECT_THROW(
        (void)RaidSession(highRiskConfig(0.0F)),
        std::invalid_argument);
    EXPECT_THROW(
        (void)RaidSession(highRiskConfig(5.0F, 3.0F, 0.0F)),
        std::invalid_argument);
}

TEST(RaidSessionTest, BeginsPreparingWithFullTime)
{
    const RaidSession session{
        RaidSessionConfig{20.0F, 4.0F}};

    EXPECT_EQ(
        session.state(),
        RaidSessionState::Preparing);
    EXPECT_FALSE(session.isActive());
    EXPECT_FALSE(session.isTerminal());
    EXPECT_FLOAT_EQ(session.raidTimeRemaining(), 20.0F);
    EXPECT_FLOAT_EQ(session.extractionTimeElapsed(), 0.0F);
    EXPECT_FLOAT_EQ(session.extractionProgress(), 0.0F);
}

TEST(RaidSessionTest, OnlyStartsOnce)
{
    RaidSession session;

    EXPECT_TRUE(session.start());
    EXPECT_EQ(session.state(), RaidSessionState::InRaid);
    EXPECT_TRUE(session.isActive());
    EXPECT_FALSE(session.start());
    EXPECT_EQ(session.state(), RaidSessionState::InRaid);
}

TEST(RaidSessionTest, PreparingSessionDoesNotUpdate)
{
    RaidSession session{
        RaidSessionConfig{20.0F, 4.0F}};

    session.update(5.0F, true);

    EXPECT_EQ(session.state(), RaidSessionState::Preparing);
    EXPECT_FLOAT_EQ(session.raidTimeRemaining(), 20.0F);
}

TEST(RaidSessionTest, ActiveRaidCountsDown)
{
    RaidSession session{
        RaidSessionConfig{20.0F, 4.0F}};
    ASSERT_TRUE(session.start());

    session.update(1.5F, false);

    EXPECT_EQ(session.state(), RaidSessionState::InRaid);
    EXPECT_FLOAT_EQ(session.raidTimeRemaining(), 18.5F);
}

TEST(RaidSessionTest, NoHardTimeLimitNeverTimesOutAndStillExtracts)
{
    RaidSession session{RaidSessionConfig{0.0F, 3.0F, false}};
    ASSERT_TRUE(session.start());

    session.update(10000.0F, false);
    EXPECT_EQ(session.state(), RaidSessionState::InRaid);
    EXPECT_FLOAT_EQ(session.raidTimeRemaining(), 0.0F);

    session.update(3.0F, true);
    EXPECT_EQ(session.state(), RaidSessionState::Extracted);
    EXPECT_FLOAT_EQ(session.extractionProgress(), 1.0F);
}

TEST(RaidSessionTest, RegularTimeTransitionsToContinuousHighRiskWithoutFailure)
{
    RaidSession session{highRiskConfig()};
    ASSERT_TRUE(session.start());

    session.update(5.0F, false, false);

    EXPECT_EQ(session.phase(), RaidPhase::HighRisk);
    EXPECT_TRUE(session.enteredHighRiskLastUpdate());
    EXPECT_TRUE(session.isActive());
    EXPECT_FALSE(session.isTerminal());
    EXPECT_FLOAT_EQ(session.raidTimeRemaining(), 0.0F);
    EXPECT_FALSE(session.normalExtractionOpen());
    EXPECT_TRUE(session.emergencyExtractionOpen());

    session.update(10000.0F, false, false);
    EXPECT_EQ(session.phase(), RaidPhase::HighRisk);
    EXPECT_EQ(session.state(), RaidSessionState::InRaid);
    EXPECT_FLOAT_EQ(session.highRiskTimeElapsed(), 10000.0F);
}

TEST(RaidSessionTest,
    ActiveTriggerEntersTheSameIrreversibleHighRiskPhase)
{
    RaidSession session{highRiskConfig(20.0F)};
    ASSERT_TRUE(session.start());

    EXPECT_TRUE(session.triggerHighRisk());
    EXPECT_EQ(session.phase(), RaidPhase::HighRisk);
    EXPECT_FLOAT_EQ(session.raidTimeRemaining(), 0.0F);
    EXPECT_TRUE(session.emergencyExtractionOpen());
    EXPECT_FALSE(session.normalExtractionOpen());
    EXPECT_FALSE(session.triggerHighRisk());

    session.update(1000.0F, false, false);
    EXPECT_TRUE(session.isActive());
    EXPECT_FLOAT_EQ(session.highRiskTimeElapsed(), 1000.0F);
}

TEST(
    RaidSessionTest,
    ActiveTriggerPreservesOneInProgressNormalExtractionGrace)
{
    RaidSession session{highRiskConfig(20.0F)};
    ASSERT_TRUE(session.start());

    session.update(1.0F, true, false);
    ASSERT_EQ(session.state(), RaidSessionState::Extracting);
    ASSERT_TRUE(session.triggerHighRisk());

    EXPECT_TRUE(session.normalExtractionGraceActive());
    EXPECT_FALSE(session.normalExtractionOpen());
    session.update(2.0F, true, false);
    EXPECT_EQ(session.state(), RaidSessionState::Extracted);
}

TEST(RaidSessionTest, HighRiskClosesNormalExtractionAndOpensEmergencySignal)
{
    RaidSession session{highRiskConfig()};
    ASSERT_TRUE(session.start());
    session.update(5.0F, false, false);

    session.update(1.0F, true, false);
    EXPECT_EQ(session.state(), RaidSessionState::InRaid);
    EXPECT_EQ(session.extractionRoute(), RaidExtractionRoute::None);

    session.update(4.0F, false, true);
    EXPECT_EQ(session.state(), RaidSessionState::Extracted);
    EXPECT_EQ(
        session.extractionRoute(),
        RaidExtractionRoute::EmergencySignal);
    EXPECT_FLOAT_EQ(session.extractionProgress(), 1.0F);
}

TEST(RaidSessionTest, ConditionalExtractionRequiresContinuousEligibility)
{
    RaidSession session{highRiskConfig()};
    ASSERT_TRUE(session.start());
    session.update(5.0F, false, false, false);
    ASSERT_TRUE(session.conditionalExtractionOpen());

    session.update(1.0F, false, false, true);
    ASSERT_EQ(session.state(), RaidSessionState::Extracting);
    ASSERT_EQ(
        session.extractionRoute(),
        RaidExtractionRoute::EmergencyConditional);
    EXPECT_FLOAT_EQ(session.extractionProgress(), 0.5F);

    session.update(0.0F, false, false, false);
    EXPECT_EQ(session.state(), RaidSessionState::InRaid);
    EXPECT_FLOAT_EQ(session.extractionProgress(), 0.0F);

    session.update(2.0F, false, false, true);
    EXPECT_EQ(session.state(), RaidSessionState::Extracted);
    EXPECT_EQ(
        session.extractionRoute(),
        RaidExtractionRoute::EmergencyConditional);
}

TEST(RaidSessionTest, NormalExtractionStartedBeforeHighRiskGetsOneCompletionGrace)
{
    RaidSession session{highRiskConfig(5.0F, 6.0F, 4.0F)};
    ASSERT_TRUE(session.start());

    session.update(4.0F, true, false);
    ASSERT_EQ(session.state(), RaidSessionState::Extracting);
    session.update(2.0F, true, false);

    EXPECT_EQ(session.phase(), RaidPhase::HighRisk);
    EXPECT_EQ(session.state(), RaidSessionState::Extracted);
    EXPECT_EQ(session.extractionRoute(), RaidExtractionRoute::Normal);
    EXPECT_FLOAT_EQ(session.extractionProgress(), 1.0F);
}

TEST(RaidSessionTest, InterruptedNormalGraceCannotRestartDuringHighRisk)
{
    RaidSession session{highRiskConfig(5.0F, 6.0F, 4.0F)};
    ASSERT_TRUE(session.start());

    session.update(4.0F, true, false);
    session.update(1.0F, true, false);
    ASSERT_EQ(session.phase(), RaidPhase::HighRisk);
    ASSERT_TRUE(session.normalExtractionGraceActive());

    session.update(0.0F, false, false);
    EXPECT_FALSE(session.normalExtractionGraceActive());
    EXPECT_EQ(session.state(), RaidSessionState::InRaid);

    session.update(10.0F, true, false);
    EXPECT_EQ(session.state(), RaidSessionState::InRaid);
    EXPECT_EQ(session.extractionRoute(), RaidExtractionRoute::None);
}

TEST(RaidSessionTest, EnteringStartsContinuousExtraction)
{
    RaidSession session{
        RaidSessionConfig{20.0F, 4.0F}};
    ASSERT_TRUE(session.start());

    session.update(1.5F, true);

    EXPECT_EQ(session.state(), RaidSessionState::Extracting);
    EXPECT_FLOAT_EQ(session.raidTimeRemaining(), 18.5F);
    EXPECT_FLOAT_EQ(session.extractionTimeElapsed(), 1.5F);
    EXPECT_FLOAT_EQ(session.extractionProgress(), 0.375F);
}

TEST(RaidSessionTest, LeavingCancelsAndClearsExtractionProgress)
{
    RaidSession session{
        RaidSessionConfig{20.0F, 4.0F}};
    ASSERT_TRUE(session.start());
    session.update(1.5F, true);

    session.update(0.0F, false);

    EXPECT_EQ(session.state(), RaidSessionState::InRaid);
    EXPECT_FLOAT_EQ(session.extractionTimeElapsed(), 0.0F);
    EXPECT_FLOAT_EQ(session.extractionProgress(), 0.0F);
    EXPECT_FLOAT_EQ(session.raidTimeRemaining(), 18.5F);
}

TEST(RaidSessionTest, ReentryRestartsExtractionFromZero)
{
    RaidSession session{
        RaidSessionConfig{20.0F, 4.0F}};
    ASSERT_TRUE(session.start());
    session.update(1.5F, true);
    session.update(0.0F, false);

    session.update(0.5F, true);

    EXPECT_EQ(session.state(), RaidSessionState::Extracting);
    EXPECT_FLOAT_EQ(session.extractionTimeElapsed(), 0.5F);
}

TEST(RaidSessionTest, ExtractionCompletesBeforeRaidTimeout)
{
    RaidSession session{
        RaidSessionConfig{20.0F, 3.0F}};
    ASSERT_TRUE(session.start());

    session.update(3.0F, true);

    EXPECT_EQ(session.state(), RaidSessionState::Extracted);
    EXPECT_TRUE(session.isTerminal());
    EXPECT_FLOAT_EQ(session.raidTimeRemaining(), 17.0F);
    EXPECT_FLOAT_EQ(session.extractionTimeElapsed(), 3.0F);
    EXPECT_FLOAT_EQ(session.extractionProgress(), 1.0F);
}

TEST(RaidSessionTest, LargeUpdateStopsAtEarlierExtractionEvent)
{
    RaidSession session{
        RaidSessionConfig{20.0F, 3.0F}};
    ASSERT_TRUE(session.start());

    session.update(100.0F, true);

    EXPECT_EQ(session.state(), RaidSessionState::Extracted);
    EXPECT_FLOAT_EQ(session.raidTimeRemaining(), 17.0F);
}

TEST(RaidSessionTest, RaidTimeoutEndsSession)
{
    RaidSession session{
        RaidSessionConfig{2.0F, 3.0F}};
    ASSERT_TRUE(session.start());

    session.update(2.0F, false);

    EXPECT_EQ(session.state(), RaidSessionState::RaidEnded);
    EXPECT_TRUE(session.isTerminal());
    EXPECT_FLOAT_EQ(session.raidTimeRemaining(), 0.0F);
}

TEST(RaidSessionTest, TimeoutDuringExtractionClearsProgress)
{
    RaidSession session{
        RaidSessionConfig{2.0F, 3.0F}};
    ASSERT_TRUE(session.start());
    session.update(1.0F, true);
    ASSERT_EQ(
        session.state(),
        RaidSessionState::Extracting);

    session.update(1.0F, true);

    EXPECT_EQ(session.state(), RaidSessionState::RaidEnded);
    EXPECT_FLOAT_EQ(session.raidTimeRemaining(), 0.0F);
    EXPECT_FLOAT_EQ(session.extractionTimeElapsed(), 0.0F);
    EXPECT_FLOAT_EQ(session.extractionProgress(), 0.0F);
}

TEST(RaidSessionTest, ExactExtractionTimeoutTieBelongsToTimeout)
{
    RaidSession session{
        RaidSessionConfig{3.0F, 3.0F}};
    ASSERT_TRUE(session.start());

    session.update(3.0F, true);

    EXPECT_EQ(session.state(), RaidSessionState::RaidEnded);
    EXPECT_FLOAT_EQ(session.raidTimeRemaining(), 0.0F);
    EXPECT_FLOAT_EQ(session.extractionTimeElapsed(), 0.0F);
}

TEST(RaidSessionTest, PlayerDeathEndsActiveRaid)
{
    RaidSession session;
    ASSERT_TRUE(session.start());
    session.update(1.0F, true);

    EXPECT_TRUE(session.markPlayerDead());
    EXPECT_EQ(session.state(), RaidSessionState::PlayerDead);
    EXPECT_TRUE(session.isTerminal());
    EXPECT_FLOAT_EQ(session.extractionTimeElapsed(), 0.0F);
}

TEST(RaidSessionTest, PlayerDeathCannotEndPreparingSession)
{
    RaidSession session;

    EXPECT_FALSE(session.markPlayerDead());
    EXPECT_EQ(session.state(), RaidSessionState::Preparing);
}

TEST(RaidSessionTest, TerminalResultIsSticky)
{
    RaidSession session{
        RaidSessionConfig{20.0F, 3.0F}};
    ASSERT_TRUE(session.start());
    session.update(3.0F, true);
    ASSERT_EQ(session.state(), RaidSessionState::Extracted);

    session.update(100.0F, false);

    EXPECT_FALSE(session.markPlayerDead());
    EXPECT_FALSE(session.start());
    EXPECT_EQ(session.state(), RaidSessionState::Extracted);
    EXPECT_FLOAT_EQ(session.raidTimeRemaining(), 17.0F);
}

TEST(RaidSessionTest, InvalidDeltaDoesNotAdvanceButCanChangeOccupancy)
{
    RaidSession session{
        RaidSessionConfig{20.0F, 3.0F}};
    ASSERT_TRUE(session.start());

    session.update(
        std::numeric_limits<float>::quiet_NaN(),
        true);

    EXPECT_EQ(session.state(), RaidSessionState::Extracting);
    EXPECT_FLOAT_EQ(session.raidTimeRemaining(), 20.0F);
    EXPECT_FLOAT_EQ(session.extractionTimeElapsed(), 0.0F);

    session.update(-1.0F, false);

    EXPECT_EQ(session.state(), RaidSessionState::InRaid);
    EXPECT_FLOAT_EQ(session.raidTimeRemaining(), 20.0F);
}

TEST(RaidSessionTest, StateNamesCoverAllPublishedStates)
{
    EXPECT_STREQ(
        raidSessionStateName(RaidSessionState::Preparing),
        "PREPARING");
    EXPECT_STREQ(
        raidSessionStateName(RaidSessionState::InRaid),
        "IN RAID");
    EXPECT_STREQ(
        raidSessionStateName(RaidSessionState::Extracting),
        "EXTRACTING");
    EXPECT_STREQ(
        raidSessionStateName(RaidSessionState::Extracted),
        "EXTRACTED");
    EXPECT_STREQ(
        raidSessionStateName(RaidSessionState::PlayerDead),
        "PLAYER DEAD");
    EXPECT_STREQ(
        raidSessionStateName(RaidSessionState::RaidEnded),
        "RAID ENDED");
}
