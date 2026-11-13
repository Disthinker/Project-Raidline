#include <gtest/gtest.h>

#include "base_emergency_power_domain.h"
#include "base_facility_layout_domain.h"
#include "base_workforce_domain.h"

namespace
{
const BaseManufacturingRecipeDefinitionId kWeaponKitRecipe{
    "base_manufacturing.weapon_maintenance_kit"};
const ContentRegistry &content = publishedContentRegistry();

ProfileState makeGeneratorProfile()
{
    ProfileState profile = makeNewAlphaProfile("base-emergency-power-test", content);
    profile.baseConstruction.facilities.emplace(
        kBaseGeneratorFacilityId, BaseConstructionState::FacilityPlacement::Installed);
    initializeBaseFacilityLayouts(profile, content);
    return profile;
}

AssetInstanceId addToStash(ProfileState &profile, const ItemDefinitionId &item)
{
    const auto &definition = content.item(item);
    const auto origin = findFirstProfileFit(
        profile, content, ProfileContainerId::stash(), definition, ItemOrientation::Degrees0);
    if (!origin)
    {
        ADD_FAILURE() << "test asset did not fit";
        return 0U;
    }
    return profile.assets.create(definition, StoredAssetLocation{ProfileContainerId::stash(), *origin});
}

BaseManufacturingReceipt startOrder(ProfileState &profile, const char *transaction = "start-order")
{
    addToStash(profile, ItemDefinitionId{"item.loot.scrap_parts"});
    addToStash(profile, ItemDefinitionId{"item.loot.electronics"});
    return executeStartBaseManufacturing(
        profile, content, {kWeaponKitRecipe}, {profile.revision, transaction});
}

BaseEmergencyPowerReceipt issuePower(
    ProfileState &profile, const BaseEmergencyPowerCommand &command, const char *transaction)
{
    return executeBaseEmergencyPower(profile, content, command, {profile.revision, transaction});
}

BaseWorldWorkAdvanceResult advance(ProfileState &profile, std::uint64_t minutes)
{
    EXPECT_EQ(advanceWorldClock(profile.worldClock, minutes).minutesApplied, minutes);
    return synchronizeBaseWorkThrough(profile, content, minutes);
}
} // namespace

TEST(BaseEmergencyPowerDomainTest, RejectsInvalidInputsAndConsumesDesignatedStashFuelExactlyOnce)
{
    ProfileState profile = makeGeneratorProfile();
    const AssetInstanceId fuel = addToStash(profile, kRaidFuelCanId);
    const AssetInstanceId otherFuel = addToStash(profile, kRaidFuelCanId);
    const auto before = profileStateFingerprint(profile);
    EXPECT_FALSE(issuePower(profile, StartBaseEmergencyPowerCommand{
        BaseEmergencyPowerTarget::Workshop, fuel, 1U}, "missing-order").succeeded);
    EXPECT_EQ(profileStateFingerprint(profile), before);

    const auto order = startOrder(profile);
    ASSERT_TRUE(order.succeeded) << order.message;
    ASSERT_EQ(baseEmergencyPowerFuel(profile), fuel);
    ASSERT_EQ(baseEmergencyPowerJob(profile, BaseEmergencyPowerTarget::Workshop), order.jobId);
    const auto wrongJob = issuePower(profile, StartBaseEmergencyPowerCommand{
        BaseEmergencyPowerTarget::Workshop, fuel, order.jobId + 1U}, "wrong-job");
    EXPECT_FALSE(wrongJob.succeeded);
    EXPECT_NE(profile.assets.find(fuel), nullptr);
    auto &fuelLocation = std::get<StoredAssetLocation>(profile.assets.findMutable(fuel)->location);
    const auto originalContainer = fuelLocation.container;
    fuelLocation.container = ProfileContainerId::baseIntake();
    const auto outsideStash = issuePower(profile, StartBaseEmergencyPowerCommand{
        BaseEmergencyPowerTarget::Workshop, fuel, order.jobId}, "intake-fuel");
    EXPECT_FALSE(outsideStash.succeeded);
    EXPECT_EQ(baseEmergencyPowerFuel(profile), otherFuel);
    fuelLocation.container = originalContainer;

    const StartBaseEmergencyPowerCommand command{BaseEmergencyPowerTarget::Workshop, fuel, order.jobId};
    const auto started = issuePower(profile, command, "start-power");
    ASSERT_TRUE(started.succeeded) << started.message;
    ASSERT_TRUE(profile.emergencyPower);
    EXPECT_EQ(profile.emergencyPower->activeJobId, order.jobId);
    EXPECT_EQ(profile.emergencyPower->remainingMinutes, 180U);
    EXPECT_EQ(profile.assets.find(fuel), nullptr);
    EXPECT_NE(profile.assets.find(otherFuel), nullptr);
    const auto committed = profileStateFingerprint(profile);
    const auto replay = issuePower(profile, command, "start-power");
    EXPECT_TRUE(replay.succeeded);
    EXPECT_TRUE(replay.alreadyCommitted);
    EXPECT_EQ(profileStateFingerprint(profile), committed);
    EXPECT_FALSE(issuePower(profile, StartBaseEmergencyPowerCommand{
        BaseEmergencyPowerTarget::Workshop, otherFuel, order.jobId}, "second-batch").succeeded);
    EXPECT_EQ(profileStateFingerprint(profile), committed);
}

TEST(BaseEmergencyPowerDomainTest, RestPauseResumeAndCancellationKeepFuelAndCompletedWork)
{
    ProfileState profile = makeGeneratorProfile();
    const auto order = startOrder(profile);
    ASSERT_TRUE(order.succeeded) << order.message;
    const auto fuel = addToStash(profile, kRaidFuelCanId);
    const auto started = issuePower(profile, StartBaseEmergencyPowerCommand{
        BaseEmergencyPowerTarget::Workshop, fuel, order.jobId}, "start-power");
    ASSERT_TRUE(started.succeeded) << started.message;

    const auto initialThreat = profile.baseSiege.siteThreatUnits;
    const auto rested = executeBaseRest(profile, content, {1U}, {profile.revision, "rest-with-power"});
    ASSERT_TRUE(rested.succeeded) << rested.message;
    EXPECT_EQ(projectBaseManufacturing(profile).remainingMinutes, 240U);
    EXPECT_EQ(profile.emergencyPower->remainingMinutes, 120U);
    EXPECT_EQ(profile.baseSiege.siteThreatUnits, initialThreat + 1U);

    const auto paused = issuePower(profile, PauseBaseEmergencyPowerCommand{}, "pause-power");
    ASSERT_TRUE(paused.succeeded) << paused.message;
    EXPECT_FALSE(advance(profile, 50U).manufacturing.completed);
    EXPECT_EQ(projectBaseManufacturing(profile).remainingMinutes, 190U);
    EXPECT_EQ(profile.emergencyPower->remainingMinutes, 120U);
    EXPECT_EQ(profile.baseSiege.siteThreatUnits, initialThreat + 1U);

    const auto resumed = issuePower(profile, ResumeBaseEmergencyPowerCommand{order.jobId}, "resume-power");
    ASSERT_TRUE(resumed.succeeded) << resumed.message;
    EXPECT_FALSE(advance(profile, 9U).manufacturing.completed);
    EXPECT_EQ(projectBaseManufacturing(profile).remainingMinutes, 172U);
    EXPECT_EQ(profile.emergencyPower->remainingMinutes, 111U);
    const auto cancelled = executeCancelBaseManufacturing(profile, content, {profile.revision, "cancel"});
    ASSERT_TRUE(cancelled.succeeded) << cancelled.message;
    ASSERT_TRUE(profile.emergencyPower);
    EXPECT_FALSE(profile.emergencyPower->activeJobId);
    EXPECT_EQ(profile.emergencyPower->remainingMinutes, 111U);

    const auto newOrder = startOrder(profile, "next-order");
    ASSERT_TRUE(newOrder.succeeded) << newOrder.message;
    EXPECT_FALSE(issuePower(profile, ResumeBaseEmergencyPowerCommand{order.jobId}, "stale-order").succeeded);
    EXPECT_TRUE(issuePower(profile, ResumeBaseEmergencyPowerCommand{newOrder.jobId}, "resume-next-order").succeeded);
    pauseBaseEmergencyPowerForFacility(profile, BaseFacilityDefinitionId{"base_facility.medical"});
    EXPECT_EQ(profile.emergencyPower->activeJobId, newOrder.jobId);
    pauseBaseEmergencyPowerForFacility(profile, kBaseGeneratorFacilityId);
    EXPECT_FALSE(profile.emergencyPower->activeJobId);
    EXPECT_EQ(profile.emergencyPower->remainingMinutes, 111U);
}

TEST(BaseEmergencyPowerDomainTest, BatchedAndMinuteStepsAgreeAtMidnightCompletionAndExhaustion)
{
    ProfileState profile = makeGeneratorProfile();
    static_cast<void>(advance(profile, 890U));
    profile.baseSiege.raidThreatUnits = 95U;
    profile.baseSiege.populationThreatUnits = 3U;
    profile.baseSiege.siteThreatUnits = 0U;
    const auto order = startOrder(profile);
    ASSERT_TRUE(order.succeeded) << order.message;
    const auto fuel = addToStash(profile, kRaidFuelCanId);
    const auto started = issuePower(profile, StartBaseEmergencyPowerCommand{
        BaseEmergencyPowerTarget::Workshop, fuel, order.jobId}, "start-power");
    ASSERT_TRUE(started.succeeded) << started.message;

    ProfileState split = profile;
    const auto completed = advance(profile, 180U);
    EXPECT_TRUE(completed.manufacturing.completed);
    EXPECT_EQ(completed.manufacturing.outputAssetId, order.outputAssetId);
    EXPECT_EQ(completed.demand.cyclesResolved, 1U);
    for (std::uint32_t minute = 1U; minute <= 180U; ++minute)
        EXPECT_EQ(advance(split, 1U).manufacturing.completed, minute == 180U);
    EXPECT_EQ(profileStateFingerprint(profile), profileStateFingerprint(split));
    EXPECT_FALSE(profile.emergencyPower);
    EXPECT_EQ(totalBaseThreat(profile.baseSiege), kBaseSiegeThreatThreshold);
    // The first hourly charge precedes midnight; population consumes the
    // final capacity at midnight before later generator charges can apply.
    EXPECT_EQ(profile.baseSiege.siteThreatUnits, 1U);
    EXPECT_EQ(profile.baseSiege.populationThreatUnits, 4U);
    EXPECT_FALSE(advance(profile, 1U).manufacturing.completed);
    EXPECT_TRUE(validateProfileState(profile, content).valid);
}

TEST(BaseEmergencyPowerDomainTest, PendingRaidAccruesBonusAndDefersPublishingWorkshopOutput)
{
    ProfileState profile = makeGeneratorProfile();
    const auto order = startOrder(profile);
    ASSERT_TRUE(order.succeeded) << order.message;
    const auto fuel = addToStash(profile, kRaidFuelCanId);
    const auto started = issuePower(profile, StartBaseEmergencyPowerCommand{
        BaseEmergencyPowerTarget::Workshop, fuel, order.jobId}, "start-power");
    ASSERT_TRUE(started.succeeded) << started.message;

    profile.pendingRaid.emplace();
    EXPECT_FALSE(advance(profile, 220U).manufacturing.completed);
    ASSERT_TRUE(profile.baseManufacturing.activeOrder);
    EXPECT_EQ(profile.baseManufacturing.activeOrder->emergencyPowerBonusMinutes, 180U);
    EXPECT_EQ(projectBaseManufacturing(profile).remainingMinutes, 0U);
    EXPECT_FALSE(profile.emergencyPower);
    const auto *output = profile.assets.find(*order.outputAssetId);
    ASSERT_NE(output, nullptr);
    EXPECT_TRUE(std::holds_alternative<BaseServiceAssetLocation>(output->location));

    profile.pendingRaid.reset();
    const auto published = synchronizeBaseWorkThrough(profile, content, 0U).manufacturing;
    EXPECT_TRUE(published.completed);
    EXPECT_EQ(published.outputAssetId, order.outputAssetId);
    EXPECT_FALSE(profile.baseManufacturing.activeOrder);
    EXPECT_FALSE(synchronizeBaseWorkThrough(profile, content, 0U).manufacturing.completed);
}

TEST(BaseEmergencyPowerDomainTest, PoweredFieldFlareOrderYieldsTwoFlaresAndKeepsUnusedFuel)
{
    ProfileState profile = makeGeneratorProfile();
    ASSERT_NE(addToStash(profile, ItemDefinitionId{"item.loot.copper_wire_spool"}), 0U);
    ASSERT_NE(addToStash(profile, ItemDefinitionId{"item.loot.electronics"}), 0U);
    const auto order = executeStartBaseManufacturing(
        profile, content, {kThrownIlluminationRecipeId}, {profile.revision, "field-flare-order"});
    ASSERT_TRUE(order.succeeded) << order.message;
    ASSERT_TRUE(order.outputAssetId);
    const auto baseDuration = projectBaseManufacturing(profile).remainingMinutes;
    ASSERT_GT(baseDuration, 2U);
    const auto poweredDuration = (baseDuration + 1U) / 2U;
    ASSERT_LT(poweredDuration, kBaseEmergencyPowerBatchMinutes);

    const auto fuel = addToStash(profile, kRaidFuelCanId);
    const auto started = issuePower(profile, StartBaseEmergencyPowerCommand{
        BaseEmergencyPowerTarget::Workshop, fuel, order.jobId}, "field-flare-power");
    ASSERT_TRUE(started.succeeded) << started.message;
    EXPECT_FALSE(advance(profile, poweredDuration - 1U).manufacturing.completed);
    const auto completion = advance(profile, 1U).manufacturing;
    EXPECT_TRUE(completion.completed);
    EXPECT_EQ(completion.outputAssetId, order.outputAssetId);
    const auto *flares = profile.assets.find(*order.outputAssetId);
    ASSERT_NE(flares, nullptr);
    EXPECT_EQ(flares->definitionId, kThrownIlluminationItemId);
    EXPECT_EQ(flares->quantity, 2U);
    EXPECT_TRUE(std::holds_alternative<StoredAssetLocation>(flares->location));
    ASSERT_TRUE(profile.emergencyPower);
    EXPECT_FALSE(profile.emergencyPower->activeJobId);
    EXPECT_EQ(profile.emergencyPower->remainingMinutes, kBaseEmergencyPowerBatchMinutes - poweredDuration);
    EXPECT_FALSE(advance(profile, 120U).manufacturing.completed);
    EXPECT_EQ(profile.emergencyPower->remainingMinutes, kBaseEmergencyPowerBatchMinutes - poweredDuration);
}

TEST(BaseEmergencyPowerDomainTest, ResidentTreatmentAcceleratesAndPausesWithoutChangingPlayerEffects)
{
    ProfileState profile = makeGeneratorProfile();
    profile.basePopulation.injuredResidents = 1U;
    profile.basePopulation.injuredByProfession[baseProfessionIndex(BaseResidentProfession::General)] = 1U;
    const ItemDefinitionId medkit{"item.medical.medkit_alpha"};
    addToStash(profile, medkit);
    profile.baseSupplyPolicy.assignments[medkit] = BaseSupplyCategory::Medical;
    const auto treatment = executeStartResidentTreatment(
        profile, content, {}, {profile.revision, "treatment"});
    ASSERT_TRUE(treatment.succeeded) << treatment.message;
    const auto playerEffects = profile.basePlayerEffects;
    const auto fuel = addToStash(profile, kRaidFuelCanId);
    const auto started = issuePower(profile, StartBaseEmergencyPowerCommand{
        BaseEmergencyPowerTarget::ResidentMedical, fuel, treatment.jobId}, "start-power");
    ASSERT_TRUE(started.succeeded) << started.message;

    EXPECT_FALSE(advance(profile, 179U).residentTreatment.completed);
    EXPECT_EQ(profile.basePopulation.injuredResidents, 1U);
    EXPECT_EQ(profile.emergencyPower->remainingMinutes, 1U);
    EXPECT_TRUE(advance(profile, 1U).residentTreatment.completed);
    EXPECT_EQ(profile.basePopulation.injuredResidents, 0U);
    EXPECT_FALSE(profile.emergencyPower);
    EXPECT_EQ(profile.basePlayerEffects.has_value(), playerEffects.has_value());
    if (profile.basePlayerEffects && playerEffects)
        EXPECT_EQ(basePlayerEffectsFingerprint(*profile.basePlayerEffects), basePlayerEffectsFingerprint(*playerEffects));
    EXPECT_EQ(profile.currentHealth, 100);
    EXPECT_TRUE(validateProfileState(profile, content).valid);
}
