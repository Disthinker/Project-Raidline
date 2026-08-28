#include "base_siege_domain.h"

#include <gtest/gtest.h>

#include "base_construction_domain.h"

namespace
{
ProfileState makeProfile(std::string id)
{
    return makeNewAlphaProfile(std::move(id), publishedContentRegistry());
}
}

TEST(BaseSiegeDomainTest, DailyThreatUsesPopulationAndActiveSite)
{
    ProfileState profile = makeProfile("siege-daily");
    ASSERT_EQ(advanceWorldClock(
        profile.worldClock, kWorldMinutesPerDay).minutesApplied,
        kWorldMinutesPerDay);

    const BaseThreatAdvanceResult result = synchronizeBaseThreatThrough(
        profile, publishedContentRegistry());

    EXPECT_TRUE(result.changed);
    EXPECT_EQ(result.daysResolved, 1U);
    EXPECT_EQ(result.populationThreatAdded, 1U);
    EXPECT_EQ(result.siteThreatAdded, 1U);
    EXPECT_EQ(totalBaseThreat(profile.baseSiege), 2U);
}

TEST(BaseSiegeDomainTest, WarningOnlyActivatesAfterThresholdAndSafety)
{
    ProfileState profile = makeProfile("siege-warning");
    profile.baseSiege.raidThreatUnits = kBaseSiegeThreatThreshold;
    const BaseThreatProjection queued = projectBaseThreat(profile);
    EXPECT_TRUE(queued.siegeQueued);
    EXPECT_EQ(queued.tier, BaseThreatTier::Queued);
    EXPECT_EQ(
        queued.safeMinutesRemaining,
        profile.baseSiege.safeUntilWorldMinute -
            profile.worldClock.elapsedWorldMinutes);
    EXPECT_STREQ(baseThreatTierName(queued.tier), "QUEUED");
    EXPECT_FALSE(activateBaseSiegeWarningIfEligible(profile));
    profile.worldClock.elapsedWorldMinutes =
        profile.baseSiege.safeUntilWorldMinute;

    const BaseThreatProjection eligible = projectBaseThreat(profile);
    EXPECT_FALSE(eligible.siegeQueued);
    EXPECT_EQ(eligible.safeMinutesRemaining, 0U);
    EXPECT_EQ(eligible.tier, BaseThreatTier::Warning);

    EXPECT_TRUE(activateBaseSiegeWarningIfEligible(profile));
    EXPECT_TRUE(profile.baseSiege.warningActive);
    EXPECT_EQ(
        profile.baseSiege.warningRemainingSeconds,
        kBaseSiegeWarningSeconds);
    EXPECT_TRUE(advanceBaseSiegeWarning(profile, 179U));
    EXPECT_EQ(profile.baseSiege.warningRemainingSeconds, 1U);
    EXPECT_TRUE(advanceBaseSiegeWarning(profile, 1U));
    EXPECT_EQ(profile.baseSiege.warningRemainingSeconds, 0U);
    EXPECT_FALSE(projectBaseThreat(profile).autoDefensePresetSaved);
    EXPECT_TRUE(projectBaseThreat(profile).requiresFirstPreset);
}

TEST(BaseSiegeDomainTest, AutoDefenseSuccessIsAtomicAndStartsSafety)
{
    ProfileState profile = makeProfile("siege-success");
    profile.baseSiege.warningActive = true;
    profile.baseSiege.warningRemainingSeconds = 100U;
    const std::uint32_t materials = profile.baseConstruction.materialUnits;
    const BaseAutoDefensePlan plan = queryBaseAutoDefense(
        profile, publishedContentRegistry());
    ASSERT_TRUE(plan.canCommit);
    ASSERT_TRUE(plan.projectedSuccess);

    const BaseAutoDefenseReceipt receipt = executeBaseAutoDefense(
        profile,
        publishedContentRegistry(),
        CommandContext{profile.revision, "siege-success-commit"});

    ASSERT_TRUE(receipt.succeeded) << receipt.message;
    EXPECT_EQ(receipt.outcome, BaseSiegeOutcome::Defended);
    EXPECT_EQ(profile.baseResources.pool.security,
              plan.availableSecurity - plan.requiredSecurity);
    EXPECT_EQ(profile.baseConstruction.materialUnits, materials + 8U);
    EXPECT_FALSE(profile.baseSiege.warningActive);
    EXPECT_EQ(totalBaseThreat(profile.baseSiege), 0U);
    EXPECT_GE(profile.baseSiege.safeUntilWorldMinute,
              profile.worldClock.elapsedWorldMinutes +
                  kBaseSiegeSuccessSafeDays * kWorldMinutesPerDay);
}

TEST(BaseSiegeDomainTest, AutoDefenseSoftFailureProtectsMinimumResidents)
{
    ProfileState profile = makeProfile("siege-failure");
    profile.baseSiege.warningActive = true;
    profile.baseResources.pool.security = 0U;
    const std::uint32_t residents = profile.basePopulation.ordinaryResidents;

    const BaseAutoDefenseReceipt receipt = executeBaseAutoDefense(
        profile,
        publishedContentRegistry(),
        CommandContext{profile.revision, "siege-failure-commit"});

    ASSERT_TRUE(receipt.succeeded) << receipt.message;
    EXPECT_EQ(receipt.outcome, BaseSiegeOutcome::SoftFailure);
    EXPECT_EQ(receipt.populationLost, 1U);
    EXPECT_EQ(profile.basePopulation.ordinaryResidents, residents - 1U);
    EXPECT_GE(profile.basePopulation.ordinaryResidents,
              kBaseSiegeMinimumResidents);
    EXPECT_FALSE(profile.baseSiege.warningActive);
}

TEST(BaseSiegeDomainTest, SoftFailureDoesNotRemoveCommittedConstructionWorker)
{
    ProfileState profile = makeProfile("siege-worker-protection");
    profile.baseConstruction.materialUnits = 4U;
    ASSERT_TRUE(executeStartBaseConstruction(
        profile,
        publishedContentRegistry(),
        StartBaseConstructionCommand{
            BaseConstructionProjectDefinitionId{
                "base_construction.dormitory.level_2"}},
        CommandContext{profile.revision, "siege-start-construction"})
                    .succeeded);
    profile.baseSiege.warningActive = true;
    profile.baseResources.pool.security = 0U;
    const std::uint32_t residents = profile.basePopulation.ordinaryResidents;

    const BaseAutoDefenseReceipt receipt = executeBaseAutoDefense(
        profile,
        publishedContentRegistry(),
        CommandContext{profile.revision, "siege-protect-workers"});

    ASSERT_TRUE(receipt.succeeded) << receipt.message;
    EXPECT_EQ(receipt.outcome, BaseSiegeOutcome::SoftFailure);
    EXPECT_EQ(receipt.populationLost, 0U);
    EXPECT_EQ(profile.basePopulation.ordinaryResidents, residents);
    EXPECT_TRUE(profile.baseConstruction.activeProject.has_value());
}

TEST(BaseSiegeDomainTest, RejectionAndReplayPreserveOrReuseTransaction)
{
    ProfileState profile = makeProfile("siege-idempotent");
    const std::uint64_t before = profileStateFingerprint(profile);
    const BaseAutoDefenseReceipt rejected = executeBaseAutoDefense(
        profile,
        publishedContentRegistry(),
        CommandContext{profile.revision, "siege-no-warning"});
    EXPECT_FALSE(rejected.succeeded);
    EXPECT_EQ(profileStateFingerprint(profile), before);

    profile.baseSiege.warningActive = true;
    const ProfileRevision revision = profile.revision;
    ASSERT_TRUE(executeBaseAutoDefense(
        profile,
        publishedContentRegistry(),
        CommandContext{revision, "siege-replay"}).succeeded);
    const std::uint64_t committed = profileStateFingerprint(profile);
    const BaseAutoDefenseReceipt replay = executeBaseAutoDefense(
        profile,
        publishedContentRegistry(),
        CommandContext{revision, "siege-replay"});
    EXPECT_TRUE(replay.succeeded);
    EXPECT_TRUE(replay.alreadyCommitted);
    EXPECT_EQ(profileStateFingerprint(profile), committed);
}
