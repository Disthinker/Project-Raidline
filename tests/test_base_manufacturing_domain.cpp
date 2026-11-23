#include <gtest/gtest.h>

#include <array>
#include <limits>
#include <string>
#include <utility>

#include "alpha_content_ids.h"
#include "base_engineering_training_domain.h"
#include "base_emergency_power_domain.h"
#include "base_facility_layout_domain.h"
#include "base_workforce_domain.h"
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
    const ItemDefinitionId &definitionId,
    std::uint32_t quantity = 1U)
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
        StoredAssetLocation{ProfileContainerId::stash(), *origin},
        quantity);
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

TEST(BaseManufacturingDomainTest, ExpediteConsumesOnlyTheSelectedStashItemAndHalvesOddRemainingWorkOnce)
{
    const auto &content = publishedContentRegistry();
    auto profile = makeProfile();
    addToStash(profile, alpha_content::lootElectronics);
    addToStash(profile, ItemDefinitionId{"item.loot.scrap_parts"});
    const auto order = startOrder(profile);
    ASSERT_TRUE(order.succeeded) << order.message;
    const auto inputs = profile.baseManufacturing.activeOrder->inputAssetIds;
    const auto keep = addToStash(profile, alpha_content::lootElectronics);
    // Select the later of two independent stash instances, after the order reserved a third one.
    const auto payment = addToStash(profile, alpha_content::lootElectronics);
    const auto oldFunds = profile.currency;
    static_cast<void>(advanceWorldClock(profile.worldClock, 13));
    const ExpediteBaseManufacturingCommand command{order.jobId, payment};
    const CommandContext tx{profile.revision, "expedite-order"};
    const auto before = profileStateFingerprint(profile);
    const auto quote = queryExpediteBaseManufacturing(profile, content, command);
    ASSERT_TRUE(quote.canCommit) << quote.message;
    EXPECT_EQ(quote.remainingMinutes, 347U);
    EXPECT_EQ(quote.newRemainingMinutes, 174U);
    EXPECT_EQ(quote.newCompletionWorldMinute, profile.worldClock.elapsedWorldMinutes + 174);
    EXPECT_EQ(quote.estimatedCompletionBefore, profile.worldClock.elapsedWorldMinutes + 347);
    EXPECT_EQ(quote.estimatedCompletionAfter, profile.worldClock.elapsedWorldMinutes + 174);
    EXPECT_EQ(profileStateFingerprint(profile), before);
    const auto result = executeExpediteBaseManufacturing(profile, content, command, tx);
    ASSERT_TRUE(result.succeeded) << result.message;
    EXPECT_EQ(result.jobId, order.jobId);
    EXPECT_FALSE(profile.assets.find(payment));
    EXPECT_NE(profile.assets.find(keep), nullptr);
    EXPECT_EQ(profile.currency, oldFunds);
    const auto &running = *profile.baseManufacturing.activeOrder;
    EXPECT_EQ(running.expediteMinutesSaved, 173U);
    EXPECT_EQ(running.outputAssetId, order.outputAssetId);
    EXPECT_EQ(running.inputAssetIds, inputs);
    EXPECT_EQ(running.committedWorkers, 1U);
    const auto committed = profileStateFingerprint(profile);
    EXPECT_TRUE(executeExpediteBaseManufacturing(profile, content, command, tx).alreadyCommitted);
    EXPECT_FALSE(executeExpediteBaseManufacturing(profile, content, {order.jobId, keep},
        {profile.revision, "duplicate-other-electronics"}).succeeded);
    EXPECT_EQ(profileStateFingerprint(profile), committed);
    auto cancelled = profile;
    ASSERT_TRUE(executeCancelBaseManufacturing(cancelled, content,
        {cancelled.revision, "cancel-expedited"}).succeeded);
    EXPECT_FALSE(cancelled.assets.find(payment));
    for (const auto id : inputs) EXPECT_NE(cancelled.assets.find(id), nullptr);
    static_cast<void>(advanceWorldClock(profile.worldClock, 173));
    EXPECT_FALSE(applyBaseManufacturingThrough(profile, content).completed);
    static_cast<void>(advanceWorldClock(profile.worldClock, 1));
    EXPECT_EQ(applyBaseManufacturingThrough(profile, content).outputAssetId, order.outputAssetId);
    EXPECT_FALSE(applyBaseManufacturingThrough(profile, content).completed);
    EXPECT_TRUE(validateProfileState(profile, content).valid);
}

TEST(BaseManufacturingDomainTest, ExpeditePreservesFuelBonusAndReservePauseAcrossReinstallation)
{
    const auto &content = publishedContentRegistry();
    const BaseFacilityDefinitionId workshop{"base_facility.workshop"};
    auto profile = makeProfile();
    profile.baseConstruction.facilities.emplace(kBaseGeneratorFacilityId, BaseConstructionState::FacilityPlacement::Installed);
    initializeBaseFacilityLayouts(profile, content);
    addToStash(profile, alpha_content::lootElectronics);
    addToStash(profile, ItemDefinitionId{"item.loot.scrap_parts"});
    const auto order = startOrder(profile);
    ASSERT_TRUE(order.succeeded) << order.message;
    const auto fuel = addToStash(profile, kRaidFuelCanId);
    const auto paid = addToStash(profile, alpha_content::lootElectronics);
    const auto power = executeBaseEmergencyPower(profile, content,
        StartBaseEmergencyPowerCommand{BaseEmergencyPowerTarget::Workshop, fuel, order.jobId}, {profile.revision, "power"});
    ASSERT_TRUE(power.succeeded) << power.message;
    const auto advance = [&](std::uint64_t minutes) {
        EXPECT_EQ(advanceWorldClock(profile.worldClock, minutes).minutesApplied, minutes);
        return synchronizeBaseWorkThrough(profile, content, minutes);
    };
    static_cast<void>(advance(31));
    const auto nominal = profile.baseManufacturing.activeOrder->completionWorldMinute;
    const auto quote = queryExpediteBaseManufacturing(profile, content, {order.jobId, paid});
    ASSERT_TRUE(quote.canCommit) << quote.message;
    EXPECT_EQ(quote.remainingMinutes, 298U);
    EXPECT_EQ(quote.newRemainingMinutes, 149U);
    EXPECT_EQ(quote.estimatedCompletionBefore, profile.worldClock.elapsedWorldMinutes + 149);
    EXPECT_EQ(quote.estimatedCompletionAfter, profile.worldClock.elapsedWorldMinutes + 75);
    auto limitedFuel = profile;
    limitedFuel.emergencyPower->remainingMinutes = 5;
    const auto limited = queryExpediteBaseManufacturing(limitedFuel, content, {order.jobId, paid});
    ASSERT_TRUE(limited.canCommit);
    EXPECT_EQ(limited.estimatedCompletionBefore, limitedFuel.worldClock.elapsedWorldMinutes + 293);
    EXPECT_EQ(limited.estimatedCompletionAfter, limitedFuel.worldClock.elapsedWorldMinutes + 144);
    const auto result = executeExpediteBaseManufacturing(profile, content, {order.jobId, paid}, {profile.revision, "rush"});
    ASSERT_TRUE(result.succeeded) << result.message;
    EXPECT_EQ(profile.baseManufacturing.activeOrder->emergencyPowerBonusMinutes, 31U);
    EXPECT_EQ(profile.baseManufacturing.activeOrder->completionWorldMinute, nominal - 149U);
    EXPECT_EQ(profile.emergencyPower->activeJobId, order.jobId);
    EXPECT_EQ(profile.emergencyPower->remainingMinutes, kBaseEmergencyPowerBatchMinutes - 31);

    pauseBaseEmergencyPowerForFacility(profile, workshop);
    profile.baseConstruction.facilities[workshop] = BaseConstructionState::FacilityPlacement::Reserve;
    profile.baseConstruction.facilityReserveStartedWorldMinutes[workshop] = profile.worldClock.elapsedWorldMinutes;
    static_cast<void>(advance(70));
    EXPECT_EQ(projectBaseManufacturing(profile).remainingMinutes, 149U);
    EXPECT_EQ(queryExpediteBaseManufacturing(profile, content, {order.jobId, paid}).message, "WORKSHOP IN RESERVE");
    const auto installed = executeInstallBaseFacility(profile, content, {workshop}, {profile.revision, "reinstall"});
    ASSERT_TRUE(installed.succeeded) << installed.message;
    EXPECT_EQ(projectBaseManufacturing(profile).remainingMinutes, 149U);
    const auto resumed = executeBaseEmergencyPower(profile, content, ResumeBaseEmergencyPowerCommand{order.jobId},
        {profile.revision, "resume-power"});
    ASSERT_TRUE(resumed.succeeded) << resumed.message;
    EXPECT_FALSE(advance(74).manufacturing.completed);
    EXPECT_EQ(projectBaseManufacturing(profile).remainingMinutes, 1U);
    EXPECT_EQ(advance(1).manufacturing.outputAssetId, order.outputAssetId);
    EXPECT_TRUE(validateProfileState(profile, content).valid);
}

TEST(BaseManufacturingDomainTest, ExpediteRejectsResearchNearCompletionStaleOrdersAndUnownedPayment)
{
    const auto &content = publishedContentRegistry();
    auto profile = makeProfile();
    addToStash(profile, alpha_content::lootElectronics);
    addToStash(profile, ItemDefinitionId{"item.loot.scrap_parts"});
    const auto order = startOrder(profile);
    ASSERT_TRUE(order.succeeded) << order.message;
    const auto payment = addToStash(profile, alpha_content::lootElectronics);
    const auto &work = *profile.baseManufacturing.activeOrder;
    const auto before = profileStateFingerprint(profile);
    for (const ExpediteBaseManufacturingCommand wrong : {ExpediteBaseManufacturingCommand{order.jobId + 1, payment},
             {order.jobId, 0}, {order.jobId, work.inputAssetIds.back()}, {order.jobId, work.outputAssetId}})
    {
        EXPECT_FALSE(executeExpediteBaseManufacturing(profile, content, wrong, {profile.revision, "wrong"}).succeeded);
        EXPECT_EQ(profileStateFingerprint(profile), before);
    }
    EXPECT_EQ(executeExpediteBaseManufacturing(profile, content, {order.jobId, payment},
        {profile.revision - 1, "stale"}).error, DomainErrorCode::StaleRevision);
    EXPECT_EQ(profileStateFingerprint(profile), before);

    auto unavailable = profile;
    unavailable.homePerimeter.activeOuting.emplace();
    EXPECT_FALSE(queryExpediteBaseManufacturing(unavailable, content, {order.jobId, payment}).canCommit);
    unavailable = profile;
    unavailable.activeEngineeringTraining.emplace();
    EXPECT_FALSE(queryExpediteBaseManufacturing(unavailable, content, {order.jobId, payment}).canCommit);
    unavailable = profile;
    unavailable.assets.findMutable(payment)->reliefBatchId = "relief";
    EXPECT_FALSE(queryExpediteBaseManufacturing(unavailable, content, {order.jobId, payment}).canCommit);
    unavailable = profile;
    unavailable.worldClock.elapsedWorldMinutes = work.completionWorldMinute - 1;
    EXPECT_EQ(queryExpediteBaseManufacturing(unavailable, content, {order.jobId, payment}).message,
        "ORDER TOO NEAR COMPLETION");
    EXPECT_TRUE(unavailable.assets.find(payment));
    auto tampered = profile;
    auto &forged = *tampered.baseManufacturing.activeOrder;
    forged.expediteMinutesSaved = static_cast<std::uint32_t>(work.completionWorldMinute - work.startedWorldMinute) - 1;
    forged.completionWorldMinute = work.startedWorldMinute + 1;
    EXPECT_FALSE(validateProfileState(tampered, content).valid);

    auto research = makeProfile();
    research.baseConstruction.workshopLevel = 2;
    addToStash(research, kArmorRepairBlueprintId);
    const auto blueprint = executeStartBaseManufacturing(research, content, {kArmorRepairResearchId},
        {research.revision, "research"});
    ASSERT_TRUE(blueprint.succeeded) << blueprint.message;
    const auto extra = addToStash(research, alpha_content::lootElectronics);
    const auto locked = executeExpediteBaseManufacturing(research, content, {blueprint.jobId, extra},
        {research.revision, "try-expedite-research"});
    EXPECT_FALSE(locked.succeeded);
    EXPECT_EQ(locked.message, "RESEARCH CANNOT BE EXPEDITED");
    EXPECT_NE(research.assets.find(extra), nullptr);
}

TEST(BaseManufacturingDomainTest,
     FreightBreachWedgesReserveExactUnitsAndCancelWithoutDuplication)
{
    const auto &content = publishedContentRegistry();
    const ItemDefinitionId scrapDefinition{"item.loot.scrap_parts"};
    const ItemDefinitionId fastenerDefinition{"item.loot.industrial_fasteners"};
    ProfileState profile = makeProfile();
    const auto scrapA = addToStash(profile, scrapDefinition);
    const auto scrapB = addToStash(profile, scrapDefinition);
    const auto fasteners = addToStash(profile, fastenerDefinition, 3U);
    const auto originalFastenerSlot = profile.assets.find(fasteners)->location;
    const auto firstFreeId = profile.assets.nextAssetId();
    const auto before = profileStateFingerprint(profile);
    const auto plan = queryStartBaseManufacturing(profile, content, {kFreightBreachRecipeId});
    ASSERT_TRUE(plan.canCommit) << plan.message;
    EXPECT_EQ(profileStateFingerprint(profile), before);
    ASSERT_EQ(plan.inputs.size(), 3U);
    EXPECT_EQ(plan.inputs[0].assetId, scrapA);
    EXPECT_EQ(plan.inputs[1].assetId, scrapB);
    EXPECT_EQ(plan.inputs[2].assetId, fasteners);
    EXPECT_EQ(plan.inputs[2].quantity, 1U);

    const CommandContext start{profile.revision, "start-freight-wedges"};
    const auto started = executeStartBaseManufacturing(
        profile, content, {kFreightBreachRecipeId}, start);
    ASSERT_TRUE(started.succeeded) << started.message;
    ASSERT_TRUE(started.outputAssetId);
    ASSERT_TRUE(profile.baseManufacturing.activeOrder);
    const auto &order = *profile.baseManufacturing.activeOrder;
    EXPECT_EQ(order.inputAssetIds, (std::vector<AssetInstanceId>{scrapA, scrapB, firstFreeId}));
    const auto *reserved = profile.assets.find(firstFreeId);
    ASSERT_NE(reserved, nullptr);
    EXPECT_EQ(reserved->location, AssetLocation{BaseServiceAssetLocation{order.jobId}});
    EXPECT_EQ(reserved->definitionId, fastenerDefinition);
    EXPECT_EQ(reserved->quantity, 1U);
    ASSERT_NE(profile.assets.find(fasteners), nullptr);
    EXPECT_EQ(profile.assets.find(fasteners)->location, originalFastenerSlot);
    EXPECT_EQ(profile.assets.find(fasteners)->quantity, 2U);
    EXPECT_EQ(profile.assets.nextAssetId(), firstFreeId + 2U);
    EXPECT_TRUE(validateProfileState(profile, content).valid);

    const auto committed = profileStateFingerprint(profile);
    const auto duplicate = executeStartBaseManufacturing(
        profile, content, {kFreightBreachRecipeId}, start);
    EXPECT_TRUE(duplicate.succeeded && duplicate.alreadyCommitted);
    EXPECT_EQ(profileStateFingerprint(profile), committed);

    const auto filler = fillStash(profile);
    const auto full = profileStateFingerprint(profile);
    const auto rejected = executeCancelBaseManufacturing(
        profile, content, {profile.revision, "cancel-freight-wedges-full"});
    EXPECT_EQ(rejected.error, DomainErrorCode::Capacity);
    EXPECT_EQ(profileStateFingerprint(profile), full);
    for (const AssetInstanceId id : filler)
    {
        ASSERT_TRUE(profile.assets.erase(id));
    }
    const auto cancelled = executeCancelBaseManufacturing(
        profile, content, {profile.revision, "cancel-freight-wedges"});
    ASSERT_TRUE(cancelled.succeeded) << cancelled.message;
    EXPECT_FALSE(profile.baseManufacturing.activeOrder);
    EXPECT_EQ(profile.assets.find(*started.outputAssetId), nullptr);
    ASSERT_NE(profile.assets.find(firstFreeId), nullptr);
    EXPECT_EQ(profile.assets.find(firstFreeId)->quantity, 1U);
    EXPECT_TRUE(std::holds_alternative<StoredAssetLocation>(profile.assets.find(firstFreeId)->location));
    EXPECT_EQ(profile.assets.find(fasteners)->quantity, 2U);
    EXPECT_EQ(profile.assets.find(fasteners)->location, originalFastenerSlot);
    EXPECT_TRUE(validateProfileState(profile, content).valid);
}

TEST(BaseManufacturingDomainTest,
     FreightBreachWedgesRejectMissingUnitsAndIdentityExhaustionAtomically)
{
    const auto &content = publishedContentRegistry();
    const ItemDefinitionId scrapDefinition{"item.loot.scrap_parts"};
    const ItemDefinitionId fastenerDefinition{"item.loot.industrial_fasteners"};
    ProfileState profile = makeProfile();
    addToStash(profile, scrapDefinition);
    addToStash(profile, fastenerDefinition, 3U);
    const auto rejected = [&](DomainErrorCode error) {
        const auto before = profileStateFingerprint(profile);
        const auto receipt = executeStartBaseManufacturing(
            profile, content, {kFreightBreachRecipeId}, {profile.revision, "start-insufficient"});
        EXPECT_FALSE(receipt.succeeded);
        EXPECT_EQ(receipt.error, error);
        EXPECT_EQ(profileStateFingerprint(profile), before);
    };
    rejected(DomainErrorCode::MissingAsset);
    addToStash(profile, scrapDefinition);
    profile.assets.setNextAssetIdForLoad(std::numeric_limits<AssetInstanceId>::max() - 1U);
    rejected(DomainErrorCode::RevisionOverflow);
}

TEST(BaseManufacturingDomainTest,
     FreightBreachWedgesCompleteOnceFromASingleFastener)
{
    const auto &content = publishedContentRegistry();
    ProfileState profile = makeProfile();
    const auto scrapA = addToStash(profile, ItemDefinitionId{"item.loot.scrap_parts"});
    const auto scrapB = addToStash(profile, ItemDefinitionId{"item.loot.scrap_parts"});
    const auto fastener = addToStash(profile, ItemDefinitionId{"item.loot.industrial_fasteners"});
    const auto started = executeStartBaseManufacturing(
        profile, content, {kFreightBreachRecipeId}, {profile.revision, "start-single-fastener"});
    ASSERT_TRUE(started.succeeded && started.outputAssetId) << started.message;
    ASSERT_TRUE(profile.baseManufacturing.activeOrder);
    const auto &order = *profile.baseManufacturing.activeOrder;
    EXPECT_EQ(order.inputAssetIds, (std::vector<AssetInstanceId>{scrapA, scrapB, fastener}));
    const auto duration = order.completionWorldMinute - order.startedWorldMinute;
    EXPECT_EQ(duration, 90U);
    EXPECT_EQ(advanceWorldClock(profile.worldClock, 90U).minutesApplied, 90U);
    const auto completed = applyBaseManufacturingThrough(profile, content);
    ASSERT_TRUE(completed.completed);
    EXPECT_FALSE(completed.outputBlocked);
    EXPECT_FALSE(applyBaseManufacturingThrough(profile, content).completed);
    EXPECT_FALSE(profile.baseManufacturing.activeOrder);
    for (const AssetInstanceId id : {scrapA, scrapB, fastener})
    {
        EXPECT_EQ(profile.assets.find(id), nullptr);
    }
    const auto *output = profile.assets.find(*started.outputAssetId);
    ASSERT_NE(output, nullptr);
    EXPECT_EQ(output->definitionId, kFreightBreachWedgesId);
    EXPECT_EQ(output->quantity, 1U);
    EXPECT_TRUE(std::holds_alternative<StoredAssetLocation>(output->location));
    EXPECT_TRUE(validateProfileState(profile, content).valid);
}

TEST(BaseManufacturingDomainTest, FireZoneContractReservesActualInputsAndDeliversOneItemAfterTwoHours)
{
    const auto &content = publishedContentRegistry();
    auto profile = makeProfile();
    const auto fuel = addToStash(profile, kRaidFuelCanId);
    const StartBaseManufacturingCommand order{kRaidFireBottleRecipeId};
    const auto before = profileStateFingerprint(profile);
    EXPECT_FALSE(executeStartBaseManufacturing(profile, content, order, {profile.revision, "fuel-only"}).succeeded);
    EXPECT_EQ(profileStateFingerprint(profile), before);
    const auto scrap = addToStash(profile, alpha_content::lootScrap);
    const CommandContext commit{profile.revision, "request-area-denial-supply"};
    const auto receipt = executeStartBaseManufacturing(profile, content, order, commit);
    ASSERT_TRUE(receipt.succeeded && receipt.outputAssetId) << receipt.message;
    ASSERT_TRUE(profile.baseManufacturing.activeOrder);
    EXPECT_EQ(profile.baseManufacturing.activeOrder->inputAssetIds, (std::vector<AssetInstanceId>{fuel, scrap}));
    EXPECT_EQ(profile.baseManufacturing.activeOrder->committedWorkers, 1U);
    EXPECT_TRUE(executeStartBaseManufacturing(profile, content, order, commit).alreadyCommitted);
    ASSERT_EQ(advanceWorldClock(profile.worldClock, 119).minutesApplied, 119U);
    EXPECT_FALSE(applyBaseManufacturingThrough(profile, content).completed);
    ASSERT_EQ(advanceWorldClock(profile.worldClock, 1).minutesApplied, 1U);
    const auto completed = applyBaseManufacturingThrough(profile, content);
    ASSERT_TRUE(completed.completed);
    EXPECT_FALSE(completed.outputBlocked);
    EXPECT_FALSE(profile.baseManufacturing.activeOrder);
    const auto *item = profile.assets.find(*receipt.outputAssetId);
    ASSERT_NE(item, nullptr);
    EXPECT_EQ(item->definitionId, kRaidFireBottleItemId);
    EXPECT_EQ(item->quantity, 1U);
    const auto after = profileStateFingerprint(profile);
    EXPECT_FALSE(applyBaseManufacturingThrough(profile, content).completed);
    EXPECT_EQ(profileStateFingerprint(profile), after);

}

TEST(BaseManufacturingDomainTest, NoiseDecoyBatchSurvivesFullStashAndCanOnlyBeCollectedOnce)
{
    const auto &content = publishedContentRegistry();
    auto profile = makeProfile();
    const StartBaseManufacturingCommand craft{
        BaseManufacturingRecipeDefinitionId{"base_manufacturing.noise_decoy"}};
    const auto scrap = addToStash(profile, alpha_content::lootScrap);
    const auto before = profileStateFingerprint(profile);
    EXPECT_FALSE(executeStartBaseManufacturing(profile, content, craft,
        {profile.revision, "missing-wire"}).succeeded);
    EXPECT_EQ(profileStateFingerprint(profile), before);

    const auto wire = addToStash(profile, ItemDefinitionId{"item.loot.copper_wire_spool"});
    const auto funds = profile.currency;
    const auto started = executeStartBaseManufacturing(profile, content, craft,
        {profile.revision, "assemble-decoys"});
    ASSERT_TRUE(started.succeeded && started.outputAssetId) << started.message;
    ASSERT_TRUE(profile.baseManufacturing.activeOrder);
    const auto order = *profile.baseManufacturing.activeOrder;
    EXPECT_EQ(order.inputAssetIds, (std::vector<AssetInstanceId>{scrap, wire}));
    EXPECT_EQ(order.committedWorkers, 1U);
    EXPECT_EQ(order.completionWorldMinute - order.startedWorldMinute, 90U);
    const auto filler = fillStash(profile);
    ASSERT_FALSE(filler.empty());
    ASSERT_EQ(advanceWorldClock(profile.worldClock, 89U).minutesApplied, 89U);
    EXPECT_FALSE(applyBaseManufacturingThrough(profile, content).completed);
    EXPECT_FALSE(queryCollectBaseManufacturing(profile, content).canCommit);
    ASSERT_EQ(advanceWorldClock(profile.worldClock, 1U).minutesApplied, 1U);
    const auto completed = applyBaseManufacturingThrough(profile, content);
    ASSERT_TRUE(completed.completed && completed.outputBlocked);
    EXPECT_EQ(profile.assets.find(scrap), nullptr);
    EXPECT_EQ(profile.assets.find(wire), nullptr);
    EXPECT_FALSE(queryCollectBaseManufacturing(profile, content).canCommit);

    ASSERT_TRUE(profile.baseManufacturing.activeOrder);
    EXPECT_TRUE(profile.baseManufacturing.activeOrder->outputReady);
    EXPECT_EQ(profile.baseManufacturing.activeOrder->outputAssetId, *started.outputAssetId);
    EXPECT_EQ(profile.baseManufacturing.activeOrder->committedWorkers, 0U);
    ASSERT_TRUE(profile.assets.erase(filler.front()));
    const CommandContext collect{profile.revision, "collect-decoys"};
    const auto collected = executeCollectBaseManufacturing(profile, content, collect);
    ASSERT_TRUE(collected.succeeded) << collected.message;
    EXPECT_EQ(collected.outputAssetId, started.outputAssetId);
    const auto *stack = profile.assets.find(*started.outputAssetId);
    ASSERT_NE(stack, nullptr);
    EXPECT_EQ(stack->definitionId, alpha_content::noiseDecoy);
    EXPECT_EQ(stack->quantity, 2U);
    EXPECT_TRUE(std::holds_alternative<StoredAssetLocation>(stack->location));
    EXPECT_EQ(profile.currency, funds);
    EXPECT_FALSE(profile.baseManufacturing.activeOrder);
    const auto after = profileStateFingerprint(profile);
    EXPECT_TRUE(executeCollectBaseManufacturing(profile, content, collect).alreadyCommitted);
    EXPECT_FALSE(executeCollectBaseManufacturing(profile, content,
        {profile.revision, "collect-again"}).succeeded);
    EXPECT_EQ(profileStateFingerprint(profile), after);
    EXPECT_TRUE(validateProfileState(profile, content).valid);
}

TEST(BaseManufacturingDomainTest, FieldSplintReservesBandageAndScrapAndProducesOneCharge)
{
    const ContentRegistry &content = publishedContentRegistry();
    ProfileState profile = makeProfile();
    const auto scrap = addToStash(profile, alpha_content::lootScrap);
    const auto spareBandage = addToStash(profile, alpha_content::bandage);
    const auto before = profileStateFingerprint(profile);
    const auto funds = profile.currency;
    const StartBaseManufacturingCommand command{kRaidFieldSplintRecipeId};

    const auto plan = queryStartBaseManufacturing(profile, content, command);
    ASSERT_TRUE(plan.canCommit) << plan.message;
    EXPECT_EQ(profileStateFingerprint(profile), before);
    EXPECT_EQ(plan.workerCount, 1U);
    EXPECT_EQ(plan.durationMinutes, 90U);
    ASSERT_EQ(plan.inputs.size(), 2U);
    EXPECT_EQ(plan.inputs[0].definitionId, alpha_content::bandage);
    EXPECT_EQ(plan.inputs[0].quantity, 1U);
    EXPECT_NE(plan.inputs[0].assetId, spareBandage);
    EXPECT_EQ(plan.inputs[1].assetId, scrap);
    EXPECT_EQ(plan.inputs[1].quantity, 1U);

    const auto started = executeStartBaseManufacturing(
        profile, content, command, {profile.revision, "craft-field-splint"});
    ASSERT_TRUE(started.succeeded && started.outputAssetId) << started.message;
    ASSERT_TRUE(profile.baseManufacturing.activeOrder);
    const auto reserved = AssetLocation{BaseServiceAssetLocation{
        profile.baseManufacturing.activeOrder->jobId}};
    for (const auto &input : plan.inputs)
    {
        ASSERT_NE(profile.assets.find(input.assetId), nullptr);
        EXPECT_EQ(profile.assets.find(input.assetId)->location, reserved);
    }
    EXPECT_EQ(advanceWorldClock(profile.worldClock, 89U).minutesApplied, 89U);
    EXPECT_FALSE(applyBaseManufacturingThrough(profile, content).completed);
    EXPECT_EQ(advanceWorldClock(profile.worldClock, 1U).minutesApplied, 1U);
    const auto completed = applyBaseManufacturingThrough(profile, content);
    ASSERT_TRUE(completed.completed);
    EXPECT_FALSE(completed.outputBlocked);
    EXPECT_FALSE(applyBaseManufacturingThrough(profile, content).completed);
    EXPECT_EQ(profile.assets.find(plan.inputs[0].assetId), nullptr);
    EXPECT_EQ(profile.assets.find(scrap), nullptr);
    ASSERT_NE(profile.assets.find(spareBandage), nullptr);
    EXPECT_EQ(profile.assets.find(spareBandage)->remainingCharges, 1U);
    EXPECT_EQ(profile.currency, funds);

    const auto *output = profile.assets.find(*started.outputAssetId);
    ASSERT_NE(output, nullptr);
    EXPECT_EQ(output->definitionId, kRaidFieldSplintId);
    EXPECT_EQ(output->quantity, 1U);
    EXPECT_EQ(output->remainingCharges, 1U);
    EXPECT_TRUE(std::holds_alternative<StoredAssetLocation>(output->location));
    EXPECT_EQ(content.item(kRaidFieldSplintId).marketRecyclePrice, 10U);
    EXPECT_LT(content.item(kRaidFieldSplintId).marketRecyclePrice,
        content.item(alpha_content::bandage).marketRecyclePrice +
        content.item(alpha_content::lootScrap).marketRecyclePrice);
    EXPECT_TRUE(validateProfileState(profile, content).valid);
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
    EXPECT_EQ(queryStartBaseEngineeringTraining(profile, publishedContentRegistry()).message,
              "the workshop production slot is occupied");
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

TEST(BaseManufacturingDomainTest, EngineeringTrainingReservesMaterialsAndConvertsOneResident)
{
    const auto &content = publishedContentRegistry();
    ProfileState profile = makeProfile();
    const auto scrapA = addToStash(profile, kBaseEngineeringTrainingInputs[0].itemDefinitionId);
    const auto scrapB = addToStash(profile, kBaseEngineeringTrainingInputs[0].itemDefinitionId);
    const auto copper = addToStash(profile, kBaseEngineeringTrainingInputs[1].itemDefinitionId);
    const auto beforePopulation = profile.basePopulation;
    const auto beforeWorkers = projectBaseWorkforce(profile);
    const auto before = profileStateFingerprint(profile);
    const auto preview = queryStartBaseEngineeringTraining(profile, content);
    ASSERT_TRUE(preview.canCommit) << preview.message;
    EXPECT_EQ(profileStateFingerprint(profile), before);
    const CommandContext context{profile.revision, "training-start"};
    const auto start = executeStartBaseEngineeringTraining(profile, content, context);
    ASSERT_TRUE(start.succeeded) << start.message;
    ASSERT_TRUE(profile.activeEngineeringTraining);
    EXPECT_EQ(profile.activeEngineeringTraining->jobId, start.jobId);
    EXPECT_EQ(profile.activeEngineeringTraining->inputAssetIds,
        (std::vector<AssetInstanceId>{scrapA, scrapB, copper}));
    EXPECT_EQ(projectBaseEngineeringTraining(profile).remainingMinutes, 720U);
    EXPECT_EQ(projectBaseWorkforce(profile).availableResidents, beforeWorkers.availableResidents - 1U);
    EXPECT_EQ(projectBaseWorkforce(profile).availableByProfession[0],
        beforeWorkers.availableByProfession[0] - 1U);
    EXPECT_FALSE(queryClearBaseWorker(profile, {BaseFacilityStaffingKind::Workshop}).canCommit);
    EXPECT_FALSE(queryStartBaseManufacturing(profile, content, {kWeaponKitRecipe}).canCommit);
    EXPECT_FALSE(queryStartBaseManufacturing(profile, content, {kArmorRepairResearchId}).canCommit);
    EXPECT_TRUE(executeStartBaseEngineeringTraining(profile, content, context).alreadyCommitted);
    for (const auto id : profile.activeEngineeringTraining->inputAssetIds)
    {
        ASSERT_NE(profile.assets.find(id), nullptr);
        EXPECT_EQ(profile.assets.find(id)->location, AssetLocation{BaseServiceAssetLocation{start.jobId}});
    }

    static_cast<void>(advanceWorldClock(profile.worldClock, 719U));
    EXPECT_FALSE(applyBaseEngineeringTrainingThrough(profile, content));
    EXPECT_EQ(projectBaseEngineeringTraining(profile).remainingMinutes, 1U);
    static_cast<void>(advanceWorldClock(profile.worldClock, 1U));
    EXPECT_TRUE(applyBaseEngineeringTrainingThrough(profile, content));
    EXPECT_FALSE(applyBaseEngineeringTrainingThrough(profile, content));
    EXPECT_FALSE(projectBaseEngineeringTraining(profile).orderPresent);
    EXPECT_EQ(profile.basePopulation.ordinaryResidents, beforePopulation.ordinaryResidents);
    EXPECT_EQ(profile.basePopulation.injuredResidents, beforePopulation.injuredResidents);
    EXPECT_EQ(profile.basePopulation.bedCapacity, beforePopulation.bedCapacity);
    EXPECT_EQ(profile.basePopulation.professionResidents[0], beforePopulation.professionResidents[0] - 1U);
    EXPECT_EQ(profile.basePopulation.professionResidents[2], beforePopulation.professionResidents[2] + 1U);
    EXPECT_EQ(projectBaseWorkforce(profile).availableResidents, beforeWorkers.availableResidents);
    EXPECT_EQ(profile.assets.find(scrapA), nullptr);
    EXPECT_EQ(profile.assets.find(scrapB), nullptr);
    EXPECT_EQ(profile.assets.find(copper), nullptr);
    EXPECT_TRUE(validateProfileState(profile, content).valid);
}

TEST(BaseManufacturingDomainTest, EngineeringTrainingCancellationRequiresRoomForAllOriginalInputs)
{
    const auto &content = publishedContentRegistry();
    ProfileState profile = makeProfile();
    const auto scrapA = addToStash(profile, kBaseEngineeringTrainingInputs[0].itemDefinitionId);
    const auto scrapB = addToStash(profile, kBaseEngineeringTrainingInputs[0].itemDefinitionId);
    const auto copper = addToStash(profile, kBaseEngineeringTrainingInputs[1].itemDefinitionId);
    addToStash(profile, ItemDefinitionId{"item.loot.electronics"});
    const auto production = startOrder(profile);
    ASSERT_TRUE(production.succeeded) << production.message;
    EXPECT_FALSE(queryStartBaseEngineeringTraining(profile, content).canCommit);
    const auto canceledProduction = executeCancelBaseManufacturing(
        profile, content, {profile.revision, "cancel-existing-weapon-kit"});
    ASSERT_TRUE(canceledProduction.succeeded) << canceledProduction.message;
    const auto started = executeStartBaseEngineeringTraining(profile, content, {profile.revision, "training"});
    ASSERT_TRUE(started.succeeded) << started.message;
    const auto plannedReturns = profile.activeEngineeringTraining->inputAssetIds;
    ASSERT_EQ(plannedReturns, (std::vector<AssetInstanceId>{scrapA, scrapB, copper}));

    const auto filler = fillStash(profile);
    static_cast<void>(advanceWorldClock(profile.worldClock, 120U));
    EXPECT_FALSE(applyBaseEngineeringTrainingThrough(profile, content));
    const auto elapsed = profile.worldClock.elapsedWorldMinutes;
    const auto full = profileStateFingerprint(profile);
    const CommandContext cancel{profile.revision, "cancel-training"};
    EXPECT_EQ(executeCancelBaseEngineeringTraining(profile, content, cancel).error, DomainErrorCode::Capacity);
    EXPECT_EQ(profileStateFingerprint(profile), full);
    for (const auto id : filler)
        ASSERT_TRUE(profile.assets.erase(id));

    const auto canceled = executeCancelBaseEngineeringTraining(profile, content, cancel);
    ASSERT_TRUE(canceled.succeeded) << canceled.message;
    EXPECT_TRUE(executeCancelBaseEngineeringTraining(profile, content, cancel).alreadyCommitted);
    EXPECT_FALSE(profile.activeEngineeringTraining);
    EXPECT_EQ(profile.worldClock.elapsedWorldMinutes, elapsed);
    for (const auto id : plannedReturns)
    {
        const auto *asset = profile.assets.find(id);
        ASSERT_NE(asset, nullptr);
        const auto *location = std::get_if<StoredAssetLocation>(&asset->location);
        ASSERT_NE(location, nullptr);
        EXPECT_EQ(location->container.kind, ProfileContainerKind::Stash);
    }
    EXPECT_TRUE(validateProfileState(profile, content).valid);
}

TEST(BaseManufacturingDomainTest, EngineeringTrainingPausesWithoutMentorAndDefersAnActiveRaid)
{
    const auto &content = publishedContentRegistry();
    ProfileState profile = makeProfile();
    addToStash(profile, kBaseEngineeringTrainingInputs[0].itemDefinitionId);
    addToStash(profile, kBaseEngineeringTrainingInputs[0].itemDefinitionId);
    addToStash(profile, kBaseEngineeringTrainingInputs[1].itemDefinitionId);
    const auto started = executeStartBaseEngineeringTraining(profile, content, {profile.revision, "training"});
    ASSERT_TRUE(started.succeeded) << started.message;

    profile.baseWorkforce.workshopWorker.reset();
    profile.basePopulation.injuredByProfession[2] = 1U;
    profile.basePopulation.injuredResidents = 1U;
    EXPECT_TRUE(projectBaseEngineeringTraining(profile).paused);
    EXPECT_FALSE(queryAssignBestBaseWorker(profile, {BaseFacilityStaffingKind::Workshop}).canCommit);
    static_cast<void>(advanceWorldClock(profile.worldClock, 120U));
    EXPECT_FALSE(applyBaseEngineeringTrainingThrough(profile, content));
    EXPECT_EQ(projectBaseEngineeringTraining(profile).remainingMinutes, 720U);

    profile.basePopulation.injuredByProfession[2] = 0U;
    profile.basePopulation.injuredResidents = 0U;
    static_cast<void>(advanceWorldClock(profile.worldClock, 120U));
    const auto assigned = executeAssignBestBaseWorker(
        profile, content, {BaseFacilityStaffingKind::Workshop}, {profile.revision, "mentor-return"});
    ASSERT_TRUE(assigned.succeeded) << assigned.message;
    EXPECT_FALSE(projectBaseEngineeringTraining(profile).paused);
    EXPECT_EQ(projectBaseEngineeringTraining(profile).remainingMinutes, 720U);

    const auto checkpoint = profile.activeEngineeringTraining;
    profile.pendingRaid.emplace();
    static_cast<void>(advanceWorldClock(profile.worldClock, 720U));
    EXPECT_FALSE(applyBaseEngineeringTrainingThrough(profile, content));
    EXPECT_EQ(profile.activeEngineeringTraining, checkpoint);
    profile.pendingRaid.reset();
    EXPECT_TRUE(applyBaseEngineeringTrainingThrough(profile, content));
    EXPECT_FALSE(profile.activeEngineeringTraining);
    EXPECT_TRUE(validateProfileState(profile, content).valid);
}
