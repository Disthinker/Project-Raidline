#include "world_clock.h"

#include <gtest/gtest.h>

#include <limits>

TEST(WorldClockTest, NewProfileStartsOnDayOneAtEight)
{
    const WorldClockProjection projection =
        projectWorldClock(WorldClockState{});

    EXPECT_EQ(projection.day, 1U);
    EXPECT_EQ(projection.hour, 8U);
    EXPECT_EQ(projection.minute, 0U);
    EXPECT_EQ(projection.timeOfDay, WorldTimeOfDay::Day);
    EXPECT_EQ(projection.completedDays, 0U);
    EXPECT_EQ(projection.minutesUntilNextDay, 16U * 60U);
}

TEST(WorldClockTest, AdvanceCrossesHourDayAndNightBoundaries)
{
    WorldClockState state;

    const WorldClockAdvanceResult first = advanceWorldClock(state, 10U * 60U);
    EXPECT_EQ(first.crossedDayCount(), 0U);
    EXPECT_EQ(projectWorldClock(state).hour, 18U);
    EXPECT_EQ(projectWorldClock(state).timeOfDay, WorldTimeOfDay::Night);

    const WorldClockAdvanceResult second = advanceWorldClock(state, 6U * 60U);
    EXPECT_EQ(second.crossedDayCount(), 1U);
    EXPECT_EQ(projectWorldClock(state).day, 2U);
    EXPECT_EQ(projectWorldClock(state).hour, 0U);
    EXPECT_EQ(projectWorldClock(state).minutesUntilNextDay, kWorldMinutesPerDay);
}

TEST(WorldClockTest, SplitAndCombinedAdvancesProduceTheSameState)
{
    WorldClockState combined;
    WorldClockState split;

    static_cast<void>(advanceWorldClock(combined, 1800U));
    static_cast<void>(advanceWorldClock(split, 600U));
    static_cast<void>(advanceWorldClock(split, 1200U));

    EXPECT_EQ(split, combined);
    EXPECT_EQ(projectWorldClock(split), projectWorldClock(combined));
}

TEST(WorldClockTest, ZeroAdvanceIsNonMutatingAndOverflowSaturates)
{
    WorldClockState state;
    const WorldClockState before = state;
    const WorldClockAdvanceResult zero = advanceWorldClock(state, 0U);
    EXPECT_EQ(state, before);
    EXPECT_EQ(zero.minutesApplied, 0U);
    EXPECT_FALSE(zero.saturated);

    state.elapsedWorldMinutes = std::numeric_limits<std::uint64_t>::max() - 5U;
    const WorldClockAdvanceResult saturated = advanceWorldClock(state, 10U);
    EXPECT_EQ(state.elapsedWorldMinutes,
              std::numeric_limits<std::uint64_t>::max());
    EXPECT_EQ(saturated.minutesApplied, 5U);
    EXPECT_TRUE(saturated.saturated);
}
