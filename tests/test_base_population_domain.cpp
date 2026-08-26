#include <gtest/gtest.h>

#include <limits>

#include "base_population_domain.h"
#include "base_manufacturing_domain.h"

TEST(BasePopulationDomainTest, ProjectionAggregatesResidentsBedsAndRations)
{
    const BasePopulationProjection supported = projectBasePopulation(
        BasePopulationState{8, 10});
    EXPECT_EQ(supported.ordinaryResidents, 8U);
    EXPECT_EQ(supported.bedCapacity, 10U);
    EXPECT_EQ(supported.bedShortfall, 0U);
    EXPECT_EQ(supported.dailyRationDemand, 8U);
    EXPECT_EQ(supported.injuredResidents, 0U);
    EXPECT_EQ(supported.healthyResidents, 8U);

    const BasePopulationProjection injured = projectBasePopulation(
        BasePopulationState{8, 10, 3});
    EXPECT_EQ(injured.injuredResidents, 3U);
    EXPECT_EQ(injured.healthyResidents, 5U);

    const BasePopulationProjection crowded = projectBasePopulation(
        BasePopulationState{13, 9});
    EXPECT_EQ(crowded.bedShortfall, 4U);
    const BaseResourceBundle demand = populationAdjustedDailyDemand(
        BasePopulationState{13, 9});
    EXPECT_EQ(demand, (BaseResourceBundle{13, 6, 5, 4}));
}

TEST(BasePopulationDomainTest, RestCrossesMidnightAndResolvesPopulationDemand)
{
    const ContentRegistry &content = publishedContentRegistry();
    ProfileState profile = makeNewAlphaProfile("population-rest", content);
    profile.worldClock.elapsedWorldMinutes = 20U * kWorldMinutesPerHour;
    const ProfileRevision revision = profile.revision;

    const BaseRestPlan plan = queryBaseRest(profile, BaseRestCommand{6});
    ASSERT_TRUE(plan.canCommit);
    EXPECT_EQ(plan.dailyCyclesCrossed, 1U);
    EXPECT_EQ(plan.arrival.day, 2U);
    EXPECT_EQ(plan.arrival.hour, 2U);
    EXPECT_EQ(plan.dailyDemand.food, 8U);

    const BaseRestReceipt receipt = executeBaseRest(
        profile,
        content,
        BaseRestCommand{6},
        CommandContext{revision, "rest-six-hours"});
    ASSERT_TRUE(receipt.succeeded) << receipt.message;
    EXPECT_EQ(receipt.worldMinutesApplied, 360U);
    EXPECT_EQ(receipt.dailyCyclesResolved, 1U);
    EXPECT_EQ(profile.revision, revision + 1U);
    EXPECT_EQ(profile.baseResources.pool, (BaseResourceBundle{32, 34, 35, 36}));
    EXPECT_EQ(profile.baseResources.resolvedDemandCycleCount, 1U);
    EXPECT_EQ(profile.baseMorale.resolvedDayCount, 1U);
    EXPECT_EQ(profile.baseMorale.tier, BaseMoraleTier::Stable);
    EXPECT_TRUE(validateProfileState(profile, content).valid);
}

TEST(BasePopulationDomainTest, RestCompletesDueDormitoryExpansion)
{
    ProfileState profile = makeNewAlphaProfile(
        "population-rest-construction",
        publishedContentRegistry());
    profile.baseConstruction.activeProject =
        ActiveBaseConstructionProject{
            BaseConstructionProjectDefinitionId{
                "base_construction.dormitory.level_2"},
            4U,
            3U,
            profile.worldClock.elapsedWorldMinutes,
            profile.worldClock.elapsedWorldMinutes + 360U};

    const BaseRestReceipt receipt = executeBaseRest(
        profile,
        publishedContentRegistry(),
        BaseRestCommand{6},
        CommandContext{profile.revision, "rest-completes-construction"});

    ASSERT_TRUE(receipt.succeeded) << receipt.message;
    EXPECT_EQ(profile.baseConstruction.dormitoryLevel, 2U);
    EXPECT_FALSE(profile.baseConstruction.activeProject.has_value());
    EXPECT_EQ(profile.basePopulation.bedCapacity, 14U);
}

TEST(BasePopulationDomainTest, RestCompletesManufacturingIntoRealStashAsset)
{
    ProfileState profile = makeNewAlphaProfile(
        "population-rest-manufacturing",
        publishedContentRegistry());
    for (const ItemDefinitionId &definitionId : {
             ItemDefinitionId{"item.loot.scrap_parts"},
             ItemDefinitionId{"item.loot.electronics"}})
    {
        const ItemDefinition &definition =
            publishedContentRegistry().item(definitionId);
        const auto origin = findFirstProfileFit(
            profile,
            publishedContentRegistry(),
            ProfileContainerId::stash(),
            definition,
            ItemOrientation::Degrees0);
        ASSERT_TRUE(origin.has_value());
        profile.assets.create(
            definition,
            StoredAssetLocation{ProfileContainerId::stash(), *origin});
    }
    const BaseManufacturingReceipt started = executeStartBaseManufacturing(
        profile,
        publishedContentRegistry(),
        StartBaseManufacturingCommand{
            BaseManufacturingRecipeDefinitionId{
                "base_manufacturing.weapon_maintenance_kit"}},
        CommandContext{profile.revision, "rest-start-manufacturing"});
    ASSERT_TRUE(started.succeeded) << started.message;

    const BaseRestReceipt rested = executeBaseRest(
        profile,
        publishedContentRegistry(),
        BaseRestCommand{6},
        CommandContext{profile.revision, "rest-completes-manufacturing"});

    ASSERT_TRUE(rested.succeeded) << rested.message;
    EXPECT_FALSE(profile.baseManufacturing.activeOrder.has_value());
    const AssetRecord *output = profile.assets.find(*started.outputAssetId);
    ASSERT_NE(output, nullptr);
    EXPECT_EQ(
        output->definitionId,
        ItemDefinitionId{"item.maintenance.weapon_kit_basic"});
    EXPECT_TRUE(std::holds_alternative<StoredAssetLocation>(output->location));
}

TEST(BasePopulationDomainTest, RestIsIdempotentAndRejectsInvalidRequests)
{
    const ContentRegistry &content = publishedContentRegistry();
    ProfileState profile = makeNewAlphaProfile("population-rest-guards", content);
    const std::uint64_t initial = profileStateFingerprint(profile);

    EXPECT_FALSE(executeBaseRest(
        profile,
        content,
        BaseRestCommand{},
        CommandContext{profile.revision, "rest-zero"}).succeeded);
    EXPECT_EQ(profileStateFingerprint(profile), initial);
    EXPECT_FALSE(executeBaseRest(
        profile,
        content,
        BaseRestCommand{13},
        CommandContext{profile.revision, "rest-too-long"}).succeeded);
    EXPECT_EQ(profileStateFingerprint(profile), initial);

    const ProfileRevision revision = profile.revision;
    ASSERT_TRUE(executeBaseRest(
        profile,
        content,
        BaseRestCommand{1},
        CommandContext{revision, "rest-once"}).succeeded);
    const std::uint64_t committed = profileStateFingerprint(profile);
    const BaseRestReceipt replay = executeBaseRest(
        profile,
        content,
        BaseRestCommand{12},
        CommandContext{profile.revision, "rest-once"});
    EXPECT_TRUE(replay.succeeded);
    EXPECT_TRUE(replay.alreadyCommitted);
    EXPECT_EQ(profileStateFingerprint(profile), committed);

    profile.pendingRaid = PendingRaidSnapshot{};
    EXPECT_FALSE(queryBaseRest(profile, BaseRestCommand{1}).canCommit);
}

TEST(BasePopulationDomainTest, RevisionOverflowIsRejectedWithoutMutation)
{
    ProfileState profile = makeNewAlphaProfile(
        "population-rest-overflow", publishedContentRegistry());
    profile.revision = std::numeric_limits<ProfileRevision>::max();
    const std::uint64_t fingerprint = profileStateFingerprint(profile);

    const BaseRestReceipt receipt = executeBaseRest(
        profile,
        publishedContentRegistry(),
        BaseRestCommand{1},
        CommandContext{profile.revision, "rest-overflow"});
    EXPECT_FALSE(receipt.succeeded);
    EXPECT_EQ(receipt.error, DomainErrorCode::RevisionOverflow);
    EXPECT_EQ(profileStateFingerprint(profile), fingerprint);
}
