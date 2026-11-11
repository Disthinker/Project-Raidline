#include <gtest/gtest.h>

#include <array>
#include <string>
#include <utility>

#include "alpha_content_ids.h"
#include "base_manufacturing_domain.h"
#include "base_site_feature_domain.h"
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
     PublishedAmmunitionRecipesReserveSelectedCaliberAndQuantity)
{
    struct RecipeCase
    {
        BaseManufacturingRecipeDefinitionId recipeId;
        ItemDefinitionId outputId;
        std::uint32_t outputQuantity;
        std::uint32_t durationMinutes;
    };
    const std::array cases{
        RecipeCase{
            BaseManufacturingRecipeDefinitionId{
                "base_manufacturing.ammunition_9x19_standard"},
            ItemDefinitionId{"item.ammunition.9mm_basic"},
            60U,
            120U},
        RecipeCase{
            BaseManufacturingRecipeDefinitionId{
                "base_manufacturing.ammunition_5_45x39_standard"},
            ItemDefinitionId{"item.ammunition.5_45x39_standard"},
            60U,
            180U},
        RecipeCase{
            BaseManufacturingRecipeDefinitionId{
                "base_manufacturing.ammunition_7_62x51_standard"},
            ItemDefinitionId{"item.ammunition.7_62x51_standard"},
            40U,
            240U},
        RecipeCase{
            BaseManufacturingRecipeDefinitionId{
                "base_manufacturing.ammunition_5_45x39_expanding"},
            kExpanding545AmmunitionId,
            18U,
            120U}};

    for (const RecipeCase &testCase : cases)
    {
        ProfileState profile = makeProfile();
        const BaseManufacturingRecipeDefinition &recipe =
            publishedContentRegistry().baseManufacturingRecipe(
                testCase.recipeId);
        for (const BaseManufacturingInputDefinition &input : recipe.inputs)
        {
            addToStash(profile, input.itemDefinitionId);
        }

        const BaseManufacturingStartPlan plan =
            queryStartBaseManufacturing(
                profile,
                publishedContentRegistry(),
                StartBaseManufacturingCommand{testCase.recipeId});
        ASSERT_TRUE(plan.canCommit) << plan.message;
        EXPECT_EQ(plan.durationMinutes, testCase.durationMinutes);
        const BaseManufacturingReceipt started =
            executeStartBaseManufacturing(
                profile,
                publishedContentRegistry(),
                StartBaseManufacturingCommand{testCase.recipeId},
                CommandContext{
                    profile.revision,
                    std::string{"start-ammunition-manufacturing-"} +
                        std::string{testCase.outputId.value()}});
        ASSERT_TRUE(started.succeeded) << started.message;
        ASSERT_TRUE(started.outputAssetId.has_value());
        const AssetRecord *output =
            profile.assets.find(*started.outputAssetId);
        ASSERT_NE(output, nullptr);
        EXPECT_EQ(output->definitionId, testCase.outputId);
        EXPECT_EQ(output->quantity, testCase.outputQuantity);
        ASSERT_TRUE(profile.baseManufacturing.activeOrder.has_value());
        EXPECT_EQ(
            profile.baseManufacturing.activeOrder->recipeDefinitionId,
            testCase.recipeId);
    }
}

TEST(BaseManufacturingDomainTest,
     Expanding545RequiresManufactureAndCannotComeFromWarehouseCatalogs)
{
    const auto &content = publishedContentRegistry();
    for (auto grant : {grantPublishedWarehouseCatalog, grantDeveloperWarehouseCatalog})
    {
        auto profile = makeProfile();
        const auto ownsExpanding = [&] {
            for (const auto &[id, asset] : profile.assets.records())
                if (asset.definitionId == kExpanding545AmmunitionId) return true;
            return false;
        };
        EXPECT_FALSE(ownsExpanding());
        ASSERT_TRUE(grant(profile, content).succeeded);
        EXPECT_FALSE(ownsExpanding());
    }
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
    profile.basePopulation.injuredByProfession =
        profile.basePopulation.professionResidents;
    addToStash(profile, ItemDefinitionId{"item.loot.scrap_parts"});
    addToStash(profile, ItemDefinitionId{"item.loot.electronics"});
    const std::uint64_t before = profileStateFingerprint(profile);

    const BaseManufacturingReceipt rejected = startOrder(profile);
    EXPECT_FALSE(rejected.succeeded);
    EXPECT_EQ(rejected.error, DomainErrorCode::Capacity);
    EXPECT_EQ(profileStateFingerprint(profile), before);

    profile.basePopulation.injuredResidents = 0U;
    profile.basePopulation.injuredByProfession = {};
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

TEST(BaseManufacturingDomainTest,
     RepairedAshworksFeatureChangesOnlyNewOrderDuration)
{
    ProfileState profile = makeProfile();
    const RegionalBaseSiteDefinitionId ashworksSite{
        "regional_base_site.ashworks_logistics_yard"};
    const RegionalOutpostDefinitionId ashworksOutpost{
        "regional_outpost.ashworks_logistics_yard"};
    profile.regionalOperations.baseSites.at(ashworksSite).unlocked = true;
    profile.regionalOperations.baseSites.at(ashworksSite)
        .uniqueFeatureRepaired = true;
    profile.regionalOperations.outposts.at(ashworksOutpost).unlocked = true;
    profile.regionalOperations.activeBaseNodeId =
        RegionNodeDefinitionId{"region_node.base.ashworks_logistics_yard"};
    profile.regionalOperations.technologyCore.baseSiteDefinitionId =
        ashworksSite;
    addToStash(profile, ItemDefinitionId{"item.loot.scrap_parts"});
    addToStash(profile, ItemDefinitionId{"item.loot.electronics"});

    const BaseManufacturingStartPlan plan = queryStartBaseManufacturing(
        profile,
        publishedContentRegistry(),
        StartBaseManufacturingCommand{kWeaponKitRecipe});
    ASSERT_TRUE(plan.canCommit) << plan.message;
    EXPECT_EQ(plan.activeSiteDurationPercent, 75U);
    EXPECT_EQ(plan.durationMinutes, 270U);
    const BaseManufacturingReceipt started = startOrder(profile);
    ASSERT_TRUE(started.succeeded) << started.message;
    const std::uint64_t frozenCompletion =
        profile.baseManufacturing.activeOrder->completionWorldMinute;

    profile.regionalOperations.activeBaseNodeId =
        RegionNodeDefinitionId{"region_node.base.greyline_yard"};
    profile.regionalOperations.technologyCore.baseSiteDefinitionId =
        RegionalBaseSiteDefinitionId{"regional_base_site.greyline_yard"};
    EXPECT_EQ(profile.baseManufacturing.activeOrder->completionWorldMinute,
              frozenCompletion);
}

TEST(BaseManufacturingDomainTest, ArmorResearchRequiresWorkshopEngineerAndStash)
{
    ProfileState profile = makeProfile();
    const ContentRegistry &content = publishedContentRegistry();
    const AssetInstanceId blueprint = addToStash(profile, kArmorRepairBlueprintId);
    const auto query = [&]
    {
        return queryStartBaseManufacturing(
            profile, content, StartBaseManufacturingCommand{kArmorRepairResearchId});
    };

    EXPECT_FALSE(query().canCommit);
    profile.baseConstruction.workshopLevel = 2U;
    profile.baseWorkforce.workshopWorker = BaseResidentProfession::General;
    EXPECT_FALSE(query().canCommit);
    profile.baseWorkforce.workshopWorker = BaseResidentProfession::Engineering;
    const auto engineeringIndex =
        static_cast<std::size_t>(BaseResidentProfession::Engineering);
    profile.basePopulation.injuredByProfession[engineeringIndex] =
        profile.basePopulation.professionResidents[engineeringIndex];
    EXPECT_FALSE(query().canCommit);
    profile.basePopulation.injuredByProfession[engineeringIndex] = 0U;

    AssetRecord *asset = profile.assets.findMutable(blueprint);
    const AssetLocation stashLocation = asset->location;
    asset->location = StoredAssetLocation{ProfileContainerId::baseIntake(), {0, 0}};
    EXPECT_EQ(query().error, DomainErrorCode::MissingAsset);
    asset->location = stashLocation;
    const auto before = profileStateFingerprint(profile);
    const BaseManufacturingStartPlan plan = query();
    ASSERT_TRUE(plan.canCommit) << plan.message;
    ASSERT_EQ(plan.inputs.size(), 1U);
    EXPECT_EQ(plan.inputs.front().assetId, blueprint);
    EXPECT_EQ(plan.durationMinutes, 306U);
    EXPECT_EQ(profileStateFingerprint(profile), before);

    const BaseManufacturingStartPlan locked = queryStartBaseManufacturing(
        profile, content, StartBaseManufacturingCommand{kArmorRepairRecipeId});
    EXPECT_FALSE(locked.canCommit);
    EXPECT_EQ(locked.message, "research the armor repair blueprint first");
}

TEST(BaseManufacturingDomainTest, ArmorResearchOccupiesSlotAndReturnsOriginalBlueprint)
{
    ProfileState profile = makeProfile();
    const ContentRegistry &content = publishedContentRegistry();
    profile.baseConstruction.workshopLevel = 2U;
    const AssetInstanceId blueprint = addToStash(profile, kArmorRepairBlueprintId);
    const AssetInstanceId spare = addToStash(profile, kArmorRepairBlueprintId);
    addToStash(profile, ItemDefinitionId{"item.loot.scrap_parts"});
    addToStash(profile, ItemDefinitionId{"item.loot.electronics"});
    ASSERT_TRUE(startOrder(profile).succeeded);
    const StartBaseManufacturingCommand research{kArmorRepairResearchId};
    EXPECT_FALSE(queryStartBaseManufacturing(profile, content, research).canCommit);
    ASSERT_TRUE(executeCancelBaseManufacturing(
        profile, content, CommandContext{profile.revision, "cancel-production"}).succeeded);

    const AssetInstanceId nextAsset = profile.assets.nextAssetId();
    const CommandContext start{profile.revision, "begin-armor-research"};
    const BaseManufacturingReceipt started = executeStartBaseManufacturing(
        profile, content, research, start);
    ASSERT_TRUE(started.succeeded) << started.message;
    EXPECT_FALSE(started.outputAssetId.has_value());
    EXPECT_EQ(profile.assets.nextAssetId(), nextAsset);
    ASSERT_TRUE(profile.baseManufacturing.activeOrder.has_value());
    EXPECT_EQ(profile.baseManufacturing.activeOrder->inputAssetIds,
              (std::vector<AssetInstanceId>{blueprint}));
    EXPECT_EQ(profile.assets.find(blueprint)->location,
              AssetLocation{BaseServiceAssetLocation{started.jobId}});
    EXPECT_TRUE(std::holds_alternative<StoredAssetLocation>(
        profile.assets.find(spare)->location));
    EXPECT_TRUE(executeStartBaseManufacturing(profile, content, research, start)
                    .alreadyCommitted);
    EXPECT_FALSE(queryStartBaseManufacturing(
        profile, content, StartBaseManufacturingCommand{kWeaponKitRecipe}).canCommit);
    EXPECT_FALSE(queryCollectBaseManufacturing(profile, content).canCommit);

    const std::vector<AssetInstanceId> filler = fillStash(profile);
    ASSERT_FALSE(filler.empty());
    static_cast<void>(advanceWorldClock(profile.worldClock, 1U));
    const auto elapsed = profile.worldClock.elapsedWorldMinutes;
    const auto before = profileStateFingerprint(profile);
    EXPECT_EQ(executeCancelBaseManufacturing(
        profile, content, CommandContext{profile.revision, "cancel-full"}).error,
        DomainErrorCode::Capacity);
    EXPECT_EQ(profileStateFingerprint(profile), before);
    ASSERT_TRUE(profile.assets.erase(filler.front()));
    const BaseManufacturingReceipt cancelled = executeCancelBaseManufacturing(
        profile, content, CommandContext{profile.revision, "cancel-open"});
    ASSERT_TRUE(cancelled.succeeded) << cancelled.message;
    EXPECT_EQ(cancelled.jobId, started.jobId);
    EXPECT_FALSE(profile.baseManufacturing.activeOrder.has_value());
    EXPECT_FALSE(profile.baseManufacturing.armorRepairRecipeUnlocked);
    EXPECT_EQ(profile.worldClock.elapsedWorldMinutes, elapsed);
    EXPECT_TRUE(std::holds_alternative<StoredAssetLocation>(
        profile.assets.find(blueprint)->location));
}

TEST(BaseManufacturingDomainTest, ArmorResearchUnlocksOnceAndManufacturesRepairTool)
{
    ProfileState profile = makeProfile();
    const ContentRegistry &content = publishedContentRegistry();
    profile.baseConstruction.workshopLevel = 2U;
    const AssetInstanceId blueprint = addToStash(profile, kArmorRepairBlueprintId);
    const StartBaseManufacturingCommand research{kArmorRepairResearchId};
    const BaseManufacturingReceipt started = executeStartBaseManufacturing(
        profile, content, research, CommandContext{profile.revision, "armor-research"});
    ASSERT_TRUE(started.succeeded) << started.message;
    const auto duration = projectBaseManufacturing(profile).remainingMinutes;
    ASSERT_GT(duration, 0U);
    static_cast<void>(advanceWorldClock(profile.worldClock, duration - 1U));
    EXPECT_FALSE(applyBaseManufacturingThrough(profile, content).completed);
    static_cast<void>(advanceWorldClock(profile.worldClock, 1U));
    const BaseManufacturingAdvanceResult result =
        applyBaseManufacturingThrough(profile, content);
    EXPECT_TRUE(result.completed);
    EXPECT_TRUE(result.researchCompleted);
    EXPECT_EQ(result.jobId, started.jobId);
    EXPECT_FALSE(result.outputAssetId.has_value());
    EXPECT_TRUE(profile.baseManufacturing.armorRepairRecipeUnlocked);
    EXPECT_EQ(profile.assets.find(blueprint), nullptr);
    EXPECT_FALSE(profile.baseManufacturing.activeOrder.has_value());
    EXPECT_FALSE(applyBaseManufacturingThrough(profile, content).completed);
    EXPECT_FALSE(queryStartBaseManufacturing(profile, content, research).canCommit);
    EXPECT_TRUE(validateProfileState(profile, content).valid);

    const AssetInstanceId copper = addToStash(
        profile, ItemDefinitionId{"item.loot.copper_wire_spool"});
    const AssetInstanceId scrap = addToStash(
        profile, ItemDefinitionId{"item.loot.scrap_parts"});
    const StartBaseManufacturingCommand production{kArmorRepairRecipeId};
    profile.baseWorkforce.workshopWorker = BaseResidentProfession::General;
    EXPECT_FALSE(queryStartBaseManufacturing(profile, content, production).canCommit);
    profile.baseWorkforce.workshopWorker = BaseResidentProfession::Engineering;
    const BaseManufacturingStartPlan plan =
        queryStartBaseManufacturing(profile, content, production);
    ASSERT_TRUE(plan.canCommit) << plan.message;
    EXPECT_EQ(plan.durationMinutes, 204U);
    const BaseManufacturingReceipt produced = executeStartBaseManufacturing(
        profile, content, production, CommandContext{profile.revision, "armor-tool"});
    ASSERT_TRUE(produced.succeeded) << produced.message;
    ASSERT_TRUE(produced.outputAssetId.has_value());
    static_cast<void>(advanceWorldClock(profile.worldClock, plan.durationMinutes));
    const BaseManufacturingAdvanceResult finished =
        applyBaseManufacturingThrough(profile, content);
    EXPECT_TRUE(finished.completed);
    EXPECT_FALSE(finished.researchCompleted);
    EXPECT_EQ(profile.assets.find(copper), nullptr);
    EXPECT_EQ(profile.assets.find(scrap), nullptr);
    const AssetRecord *output = profile.assets.find(*produced.outputAssetId);
    ASSERT_NE(output, nullptr);
    EXPECT_EQ(output->definitionId, ItemDefinitionId{"item.maintenance.armor_kit_basic"});
    EXPECT_EQ(output->remainingCharges, content.item(output->definitionId).maximumCharges);
    EXPECT_TRUE(std::holds_alternative<StoredAssetLocation>(output->location));
    EXPECT_TRUE(validateProfileState(profile, content).valid);
}
