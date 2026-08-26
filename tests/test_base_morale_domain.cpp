#include <gtest/gtest.h>

#include "base_morale_domain.h"

namespace
{
ProfileState profile()
{
    return makeNewAlphaProfile("morale-test-profile", publishedContentRegistry());
}

BaseDailySystemsResult advanceDays(ProfileState &state, std::uint64_t days)
{
    static_cast<void>(advanceWorldClock(
        state.worldClock,
        days * kWorldMinutesPerDay));
    return synchronizeBaseDailySystemsThrough(
        state,
        publishedContentRegistry());
}
}

TEST(BaseMoraleDomainTest, NewProfileHasStableIndependentMoraleAndFrozenEvent)
{
    const ProfileState state = profile();

    EXPECT_EQ(state.baseMorale.tier, BaseMoraleTier::Stable);
    EXPECT_EQ(state.baseMorale.resolvedDayCount, 0U);
    ASSERT_TRUE(state.baseCommunityEvent.definitionId.valid());
    EXPECT_EQ(
        state.baseCommunityEvent.definitionId,
        selectBaseCommunityEvent(
            state.profileId,
            0U,
            publishedContentRegistry()));
    EXPECT_EQ(state.baseResources.pool.morale, 40U);
}

TEST(BaseMoraleDomainTest, DailyShortageFallsOneTierAndCannotBeCancelledByComfort)
{
    ProfileState state = profile();
    state.baseResources.pool = {};
    state.baseMorale.pendingFulfilledWishCount = 3U;

    const BaseDailySystemsResult result = advanceDays(state, 1U);

    ASSERT_TRUE(result.morale.changed);
    EXPECT_EQ(result.morale.previousTier, BaseMoraleTier::Stable);
    EXPECT_EQ(state.baseMorale.tier, BaseMoraleTier::Low);
    EXPECT_EQ(state.baseMorale.trend, BaseMoraleTrend::Falling);
    EXPECT_EQ(state.baseMorale.consecutiveLowDays, 1U);
    EXPECT_GT(state.baseMorale.lastLedger.resourceShortfall.food, 0U);
    EXPECT_EQ(state.baseMorale.lastLedger.fulfilledWishCount, 3U);
    EXPECT_EQ(state.baseMorale.pendingFulfilledWishCount, 0U);
}

TEST(BaseMoraleDomainTest, SupportedOperationRecoversLowMoraleAfterTwoDays)
{
    ProfileState state = profile();
    state.baseMorale.tier = BaseMoraleTier::Low;
    state.baseMorale.consecutiveLowDays = 4U;
    state.baseResources.pool = {100U, 100U, 100U, 100U};

    static_cast<void>(advanceDays(state, 1U));
    EXPECT_EQ(state.baseMorale.tier, BaseMoraleTier::Low);
    EXPECT_EQ(state.baseMorale.supportedRecoveryDays, 1U);

    static_cast<void>(advanceDays(state, 1U));
    EXPECT_EQ(state.baseMorale.tier, BaseMoraleTier::Stable);
    EXPECT_EQ(state.baseMorale.trend, BaseMoraleTrend::Rising);
    EXPECT_EQ(state.baseMorale.consecutiveLowDays, 0U);
    EXPECT_EQ(state.baseMorale.supportedRecoveryDays, 0U);
}

TEST(BaseMoraleDomainTest, FulfilledWishIsDeferredUntilDailyBoundary)
{
    ProfileState state = profile();
    state.baseResources.pool = {100U, 100U, 100U, 100U};
    state.baseMorale.pendingFulfilledWishCount = 1U;

    EXPECT_EQ(state.baseMorale.tier, BaseMoraleTier::Stable);
    static_cast<void>(advanceDays(state, 1U));

    EXPECT_EQ(state.baseMorale.tier, BaseMoraleTier::High);
    EXPECT_EQ(state.baseMorale.lastLedger.fulfilledWishCount, 1U);
}

TEST(BaseMoraleDomainTest, EventRotationIsStableAndAppliesOnFollowingDay)
{
    ProfileState state = profile();
    state.baseResources.pool = {100U, 100U, 100U, 100U};

    static_cast<void>(advanceDays(state, 5U));

    EXPECT_EQ(state.baseMorale.tier, BaseMoraleTier::Stable);
    EXPECT_EQ(state.baseCommunityEvent.cycleIndex, 1U);
    const BaseCommunityEventDefinition &event =
        publishedContentRegistry().baseCommunityEvent(
            state.baseCommunityEvent.definitionId);
    if (event.moraleEffect > 0)
    {
        EXPECT_EQ(state.baseMorale.pendingPositiveEventCount, 1U);
    }
    else
    {
        EXPECT_EQ(state.baseMorale.pendingNegativeEventCount, 1U);
    }

    static_cast<void>(advanceDays(state, 1U));
    EXPECT_EQ(
        state.baseMorale.tier,
        event.moraleEffect > 0 ? BaseMoraleTier::High : BaseMoraleTier::Low);
}

TEST(BaseMoraleDomainTest, LargeEventCatchUpUsesBoundedRotationSummary)
{
    ProfileState state = profile();
    state.worldClock.elapsedWorldMinutes =
        1000000ULL * publishedContentRegistry().baseMorale().eventCycleDays *
        kWorldMinutesPerDay;

    const BaseCommunityEventSyncResult result =
        synchronizeBaseCommunityEventThrough(
            state,
            publishedContentRegistry());

    EXPECT_TRUE(result.changed);
    EXPECT_EQ(result.cyclesAdvanced, 1000000U);
    EXPECT_EQ(
        result.positiveEventsAdded + result.negativeEventsAdded,
        1000000U);
    EXPECT_EQ(state.baseCommunityEvent.cycleIndex, 1000000U);
}

TEST(BaseMoraleDomainTest, ManufacturingDurationUsesFrozenTierPercent)
{
    const BaseMoraleDefinition &rules = publishedContentRegistry().baseMorale();

    EXPECT_EQ(applyBaseMoraleDurationPercent(
                  360U, BaseMoraleTier::Low, rules),
              432U);
    EXPECT_EQ(applyBaseMoraleDurationPercent(
                  360U, BaseMoraleTier::Stable, rules),
              360U);
    EXPECT_EQ(applyBaseMoraleDurationPercent(
                  360U, BaseMoraleTier::High, rules),
              324U);
}
