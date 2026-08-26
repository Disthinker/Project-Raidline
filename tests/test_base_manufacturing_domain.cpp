#include <gtest/gtest.h>

#include <array>
#include <utility>

#include "alpha_content_ids.h"
#include "base_manufacturing_domain.h"
#include "world_clock.h"

namespace
{
const BaseManufacturingRecipeDefinitionId kWeaponKitRecipe{
    "base_manufacturing.weapon_maintenance_kit"};

ProfileState makeProfile()
{
    return makeNewAlphaProfile(
        "base-manufacturing-test",
        publishedContentRegistry());
}

AssetInstanceId addToStash(
    ProfileState &profile,
    const ItemDefinitionId &definitionId)
{
    const ContentRegistry &content = publishedContentRegistry();
    const ItemDefinition &definition = content.item(definitionId);
    const auto origin = findFirstProfileFit(
        profile,
        content,
        ProfileContainerId::stash(),
        definition,
        ItemOrientation::Degrees0);
    EXPECT_TRUE(origin.has_value());
    return profile.assets.create(
        definition,
        StoredAssetLocation{ProfileContainerId::stash(), *origin});
}

std::vector<AssetInstanceId> fillStash(ProfileState &profile)
{
    const ContentRegistry &content = publishedContentRegistry();
    const ItemDefinition &filler = content.item(alpha_content::lootCola);
    std::vector<AssetInstanceId> ids;
    while (const auto origin = findFirstProfileFit(
               profile,
               content,
               ProfileContainerId::stash(),
               filler,
               ItemOrientation::Degrees0))
    {
        ids.push_back(profile.assets.create(
            filler,
            StoredAssetLocation{ProfileContainerId::stash(), *origin}));
    }
    return ids;
}

BaseManufacturingReceipt startOrder(ProfileState &profile)
{
    return executeStartBaseManufacturing(
        profile,
        publishedContentRegistry(),
        StartBaseManufacturingCommand{kWeaponKitRecipe},
        CommandContext{profile.revision, "start-manufacturing"});
}
}

TEST(BaseManufacturingDomainTest,
     StartReservesExactInputsOutputIdentityAndWorker)
{
    ProfileState profile = makeProfile();
    const AssetInstanceId scrap = addToStash(
        profile, ItemDefinitionId{"item.loot.scrap_parts"});
    const AssetInstanceId electronics = addToStash(
        profile, ItemDefinitionId{"item.loot.electronics"});
    const AssetInstanceId highWaterBefore = profile.assets.nextAssetId();

    const BaseManufacturingStartPlan plan = queryStartBaseManufacturing(
        profile,
        publishedContentRegistry(),
        StartBaseManufacturingCommand{kWeaponKitRecipe});
    ASSERT_TRUE(plan.canCommit) << plan.message;
    ASSERT_EQ(plan.inputs.size(), 2U);
    EXPECT_EQ(plan.inputs[0].assetId, scrap);
    EXPECT_EQ(plan.inputs[1].assetId, electronics);
    EXPECT_EQ(plan.workerCount, 1U);
    EXPECT_EQ(plan.durationMinutes, 360U);

    const BaseManufacturingReceipt receipt = startOrder(profile);
    ASSERT_TRUE(receipt.succeeded) << receipt.message;
    ASSERT_TRUE(receipt.outputAssetId.has_value());
    ASSERT_TRUE(profile.baseManufacturing.activeOrder.has_value());
    const BaseManufacturingOrder &order =
        *profile.baseManufacturing.activeOrder;
    EXPECT_EQ(order.inputAssetIds,
              (std::vector<AssetInstanceId>{scrap, electronics}));
    EXPECT_EQ(order.outputAssetId, *receipt.outputAssetId);
    EXPECT_FALSE(order.outputReady);
    EXPECT_EQ(profile.assets.nextAssetId(), highWaterBefore + 1U);
    for (AssetInstanceId id : {scrap, electronics, order.outputAssetId})
    {
        const AssetRecord *asset = profile.assets.find(id);
        ASSERT_NE(asset, nullptr);
        const auto *service =
            std::get_if<BaseServiceAssetLocation>(&asset->location);
        ASSERT_NE(service, nullptr);
        EXPECT_EQ(service->jobId, order.jobId);
    }
    EXPECT_TRUE(validateProfileState(
        profile, publishedContentRegistry()).valid);
}

TEST(BaseManufacturingDomainTest,
     CompletionConsumesInputsAndPlacesRealOutputInStash)
{
    ProfileState profile = makeProfile();
    const AssetInstanceId scrap = addToStash(
        profile, ItemDefinitionId{"item.loot.scrap_parts"});
    const AssetInstanceId electronics = addToStash(
        profile, ItemDefinitionId{"item.loot.electronics"});
    const BaseManufacturingReceipt started = startOrder(profile);
    ASSERT_TRUE(started.succeeded);
    ASSERT_TRUE(advanceWorldClock(profile.worldClock, 360U)
                    .minutesApplied == 360U);

    const BaseManufacturingAdvanceResult result =
        applyBaseManufacturingThrough(profile, publishedContentRegistry());
    EXPECT_TRUE(result.completed);
    EXPECT_FALSE(result.outputBlocked);
    EXPECT_EQ(profile.assets.find(scrap), nullptr);
    EXPECT_EQ(profile.assets.find(electronics), nullptr);
    ASSERT_TRUE(started.outputAssetId.has_value());
    const AssetRecord *output = profile.assets.find(*started.outputAssetId);
    ASSERT_NE(output, nullptr);
    EXPECT_EQ(output->definitionId,
              ItemDefinitionId{"item.maintenance.weapon_kit_basic"});
    EXPECT_TRUE(std::holds_alternative<StoredAssetLocation>(output->location));
    EXPECT_FALSE(profile.baseManufacturing.activeOrder.has_value());
    EXPECT_TRUE(validateProfileState(
        profile, publishedContentRegistry()).valid);
}

TEST(BaseManufacturingDomainTest,
     FullStashKeepsReadyOutputUntilPlayerCollectsIt)
{
    ProfileState profile = makeProfile();
    addToStash(profile, ItemDefinitionId{"item.loot.scrap_parts"});
    addToStash(profile, ItemDefinitionId{"item.loot.electronics"});
    const BaseManufacturingReceipt started = startOrder(profile);
    ASSERT_TRUE(started.succeeded);
    std::vector<AssetInstanceId> filler = fillStash(profile);
    ASSERT_FALSE(filler.empty());
    static_cast<void>(advanceWorldClock(profile.worldClock, 360U));

    const BaseManufacturingAdvanceResult result =
        applyBaseManufacturingThrough(profile, publishedContentRegistry());
    ASSERT_TRUE(result.completed);
    EXPECT_TRUE(result.outputBlocked);
    ASSERT_TRUE(profile.baseManufacturing.activeOrder.has_value());
    EXPECT_TRUE(profile.baseManufacturing.activeOrder->outputReady);
    EXPECT_EQ(profile.baseManufacturing.activeOrder->committedWorkers, 0U);
    EXPECT_FALSE(queryCollectBaseManufacturing(
        profile, publishedContentRegistry()).canCommit);

    for (AssetInstanceId id : filler)
    {
        static_cast<void>(profile.assets.erase(id));
    }
    const BaseManufacturingReceipt collected =
        executeCollectBaseManufacturing(
            profile,
            publishedContentRegistry(),
            CommandContext{profile.revision, "collect-manufacturing"});
    ASSERT_TRUE(collected.succeeded) << collected.message;
    ASSERT_TRUE(collected.outputAssetId.has_value());
    EXPECT_TRUE(std::holds_alternative<StoredAssetLocation>(
        profile.assets.find(*collected.outputAssetId)->location));
    EXPECT_FALSE(profile.baseManufacturing.activeOrder.has_value());
}

TEST(BaseManufacturingDomainTest,
     CancellationRequiresAtomicStashCapacityAndReturnsInputs)
{
    ProfileState profile = makeProfile();
    const AssetInstanceId scrap = addToStash(
        profile, ItemDefinitionId{"item.loot.scrap_parts"});
    const AssetInstanceId electronics = addToStash(
        profile, ItemDefinitionId{"item.loot.electronics"});
    const BaseManufacturingReceipt started = startOrder(profile);
    ASSERT_TRUE(started.succeeded);
    const std::vector<AssetInstanceId> filler = fillStash(profile);
    const std::uint64_t before = profileStateFingerprint(profile);

    const BaseManufacturingReceipt blocked =
        executeCancelBaseManufacturing(
            profile,
            publishedContentRegistry(),
            CommandContext{profile.revision, "cancel-blocked"});
    EXPECT_FALSE(blocked.succeeded);
    EXPECT_EQ(blocked.error, DomainErrorCode::Capacity);
    EXPECT_EQ(profileStateFingerprint(profile), before);

    for (AssetInstanceId id : filler)
    {
        static_cast<void>(profile.assets.erase(id));
    }
    const BaseManufacturingReceipt cancelled =
        executeCancelBaseManufacturing(
            profile,
            publishedContentRegistry(),
            CommandContext{profile.revision, "cancel-open"});
    ASSERT_TRUE(cancelled.succeeded) << cancelled.message;
    EXPECT_TRUE(std::holds_alternative<StoredAssetLocation>(
        profile.assets.find(scrap)->location));
    EXPECT_TRUE(std::holds_alternative<StoredAssetLocation>(
        profile.assets.find(electronics)->location));
    EXPECT_FALSE(profile.baseManufacturing.activeOrder.has_value());
}

TEST(BaseManufacturingDomainTest,
     RejectionsPreserveProfileAndWorkerBudget)
{
    ProfileState profile = makeProfile();
    profile.basePopulation.injuredResidents =
        profile.basePopulation.ordinaryResidents;
    addToStash(profile, ItemDefinitionId{"item.loot.scrap_parts"});
    addToStash(profile, ItemDefinitionId{"item.loot.electronics"});
    const std::uint64_t before = profileStateFingerprint(profile);

    const BaseManufacturingReceipt rejected = startOrder(profile);
    EXPECT_FALSE(rejected.succeeded);
    EXPECT_EQ(rejected.error, DomainErrorCode::Capacity);
    EXPECT_EQ(profileStateFingerprint(profile), before);

    profile.basePopulation.injuredResidents = 0U;
    const ProfileRevision revision = profile.revision;
    const BaseManufacturingReceipt stale = executeStartBaseManufacturing(
        profile,
        publishedContentRegistry(),
        StartBaseManufacturingCommand{kWeaponKitRecipe},
        CommandContext{revision + 1U, "stale-manufacturing"});
    EXPECT_FALSE(stale.succeeded);
    EXPECT_EQ(stale.error, DomainErrorCode::StaleRevision);
}

TEST(BaseManufacturingDomainTest,
     MoraleFreezesLowStableAndHighDurationWhenOrderStarts)
{
    const std::array cases{
        std::pair{BaseMoraleTier::Low, 432U},
        std::pair{BaseMoraleTier::Stable, 360U},
        std::pair{BaseMoraleTier::High, 324U}};
    for (const auto &[tier, expectedDuration] : cases)
    {
        ProfileState profile = makeProfile();
        profile.baseMorale.tier = tier;
        profile.baseMorale.consecutiveLowDays =
            tier == BaseMoraleTier::Low ? 1U : 0U;
        addToStash(profile, ItemDefinitionId{"item.loot.scrap_parts"});
        addToStash(profile, ItemDefinitionId{"item.loot.electronics"});
        const BaseManufacturingStartPlan plan = queryStartBaseManufacturing(
            profile,
            publishedContentRegistry(),
            StartBaseManufacturingCommand{kWeaponKitRecipe});
        ASSERT_TRUE(plan.canCommit) << plan.message;
        EXPECT_EQ(plan.durationMinutes, expectedDuration);
        const BaseManufacturingReceipt started = startOrder(profile);
        ASSERT_TRUE(started.succeeded) << started.message;
        const std::uint64_t frozenCompletion =
            profile.baseManufacturing.activeOrder->completionWorldMinute;
        profile.baseMorale.tier = tier == BaseMoraleTier::Low
            ? BaseMoraleTier::High
            : BaseMoraleTier::Low;
        profile.baseMorale.consecutiveLowDays =
            profile.baseMorale.tier == BaseMoraleTier::Low ? 1U : 0U;
        EXPECT_EQ(
            profile.baseManufacturing.activeOrder->completionWorldMinute,
            frozenCompletion);
    }
}
