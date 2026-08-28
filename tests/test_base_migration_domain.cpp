#include <gtest/gtest.h>

#include "alpha_content_ids.h"
#include "base_manufacturing_domain.h"
#include "base_migration_domain.h"
#include "base_siege_domain.h"
#include "regional_operations_domain.h"

namespace
{
const RegionalBaseSiteDefinitionId kGreylineSite{
    "regional_base_site.greyline_yard"};
const RegionalBaseSiteDefinitionId kAshworksSite{
    "regional_base_site.ashworks_logistics_yard"};
const RegionalOutpostDefinitionId kGreylineOutpost{
    "regional_outpost.greyline_yard"};
const RegionalOutpostDefinitionId kAshworksOutpost{
    "regional_outpost.ashworks_logistics_yard"};
const BaseFacilityDefinitionId kKitchenWater{
    "base_facility.kitchen_water"};
const BaseFacilityDefinitionId kWorkshop{
    "base_facility.workshop"};

void prepareAshworks(ProfileState &profile)
{
    profile.regionalOperations.baseSites.at(kAshworksSite).unlocked = true;
    RegionalOutpostState &outpost =
        profile.regionalOperations.outposts.at(kAshworksOutpost);
    outpost.unlocked = true;
    outpost.established = true;
}

void completeKitchenWater(ProfileState &profile)
{
    profile.baseConstruction.kitchenWaterLevel = 1U;
    profile.baseConstruction.facilities[kKitchenWater] =
        BaseConstructionState::FacilityPlacement::Installed;
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
}

TEST(BaseMigrationDomainTest,
     QueryIsPureAndRequiresStagingOutpostAndCoreFacilities)
{
    const ContentRegistry &content = publishedContentRegistry();
    ProfileState profile = makeNewAlphaProfile(
        "migration-query", content);
    profile.regionalOperations.baseSites.at(kAshworksSite).unlocked = true;
    const std::uint64_t unlocked = profileStateFingerprint(profile);

    const BaseMigrationPlan noOutpost = queryBaseMigration(
        profile, content, BaseMigrationCommand{kAshworksSite});
    EXPECT_FALSE(noOutpost.canCommit);
    EXPECT_EQ(noOutpost.error, DomainErrorCode::IllegalDestination);
    EXPECT_EQ(profileStateFingerprint(profile), unlocked);

    prepareAshworks(profile);
    const std::uint64_t staged = profileStateFingerprint(profile);
    const BaseMigrationPlan noKitchen = queryBaseMigration(
        profile, content, BaseMigrationCommand{kAshworksSite});
    EXPECT_FALSE(noKitchen.canCommit);
    EXPECT_EQ(noKitchen.error, DomainErrorCode::Capacity);
    EXPECT_EQ(noKitchen.missingRequiredFacilities,
              std::vector<BaseFacilityDefinitionId>{kKitchenWater});
    EXPECT_EQ(profileStateFingerprint(profile), staged);

    completeKitchenWater(profile);
    const std::uint64_t ready = profileStateFingerprint(profile);
    const BaseMigrationPlan plan = queryBaseMigration(
        profile, content, BaseMigrationCommand{kAshworksSite});
    ASSERT_TRUE(plan.canCommit) << plan.message;
    EXPECT_EQ(plan.sourceSiteDefinitionId, kGreylineSite);
    EXPECT_EQ(plan.targetSiteDefinitionId, kAshworksSite);
    EXPECT_EQ(plan.migrationMinutes, 720U);
    EXPECT_EQ(plan.facilitiesEnteringReserve,
              std::vector<BaseFacilityDefinitionId>{kWorkshop});
    EXPECT_EQ(profileStateFingerprint(profile), ready);
}

TEST(BaseMigrationDomainTest,
     CommitMovesUniqueCorePausesFacilityWorkAndConvertsOldBaseToOutpost)
{
    const ContentRegistry &content = publishedContentRegistry();
    ProfileState profile = makeNewAlphaProfile(
        "migration-commit", content);
    prepareAshworks(profile);
    completeKitchenWater(profile);
    addToStash(profile, ItemDefinitionId{"item.loot.scrap_parts"});
    addToStash(profile, ItemDefinitionId{"item.loot.electronics"});
    ASSERT_TRUE(executeStartBaseManufacturing(
        profile,
        content,
        StartBaseManufacturingCommand{
            BaseManufacturingRecipeDefinitionId{
                "base_manufacturing.weapon_maintenance_kit"}},
        CommandContext{profile.revision, "migration-start-manufacturing"})
                    .succeeded);
    const std::uint64_t originalManufacturingDeadline =
        profile.baseManufacturing.activeOrder->completionWorldMinute;
    profile.baseConstruction.materialUnits = 6U;
    ASSERT_TRUE(executeStartBaseConstruction(
        profile,
        content,
        StartBaseConstructionCommand{
            BaseConstructionProjectDefinitionId{
                "base_construction.workshop.level_2"}},
        CommandContext{profile.revision, "migration-start-workshop"})
                    .succeeded);
    const std::uint64_t startMinute = profile.worldClock.elapsedWorldMinutes;
    const std::uint64_t originalDeadline =
        profile.baseConstruction.activeProject->completionWorldMinute;

    const BaseMigrationReceipt receipt = executeBaseMigration(
        profile,
        content,
        BaseMigrationCommand{kAshworksSite},
        CommandContext{profile.revision, "migration-greyline-ashworks"});

    ASSERT_TRUE(receipt.succeeded) << receipt.message;
    EXPECT_FALSE(receipt.alreadyCommitted);
    EXPECT_EQ(profile.worldClock.elapsedWorldMinutes, startMinute + 720U);
    EXPECT_EQ(profile.regionalOperations.activeBaseNodeId,
              RegionNodeDefinitionId{
                  "region_node.base.ashworks_logistics_yard"});
    EXPECT_EQ(profile.regionalOperations.technologyCore.instanceId,
              "technology_core.primary");
    EXPECT_EQ(profile.regionalOperations.technologyCore.baseSiteDefinitionId,
              kAshworksSite);
    EXPECT_TRUE(profile.regionalOperations.outposts.at(kGreylineOutpost)
                    .established);
    EXPECT_FALSE(profile.regionalOperations.outposts.at(kAshworksOutpost)
                     .established);
    EXPECT_EQ(profile.baseConstruction.facilities.at(kWorkshop),
              BaseConstructionState::FacilityPlacement::Reserve);
    ASSERT_TRUE(profile.baseConstruction.activeProject.has_value());
    EXPECT_EQ(profile.baseConstruction.activeProject->completionWorldMinute,
              originalDeadline + 720U);
    ASSERT_TRUE(profile.baseManufacturing.activeOrder.has_value());
    EXPECT_EQ(profile.baseManufacturing.activeOrder->completionWorldMinute,
              originalManufacturingDeadline + 720U);
    ASSERT_EQ(profile.baseConstruction.facilityReserveStartedWorldMinutes.at(
                  kWorkshop),
              profile.worldClock.elapsedWorldMinutes);
    ASSERT_TRUE(validateProfileState(profile, content).valid);

    const RegionalRoutePlan route = queryRegionalRoute(
        profile, content, MapDefinitionId{"map.raid.industrial"});
    ASSERT_TRUE(route.reachable) << route.message;
    EXPECT_EQ(route.travelMinutes, 40U);

    ASSERT_EQ(advanceWorldClock(profile.worldClock, 900U).minutesApplied, 900U);
    ASSERT_TRUE(validateProfileState(profile, content).valid);
    EXPECT_EQ(projectBaseConstruction(profile, content).remainingMinutes, 720U);
    EXPECT_EQ(projectBaseManufacturing(profile).remainingMinutes, 360U);
    const InstallBaseFacilityReceipt installed = executeInstallBaseFacility(
        profile,
        content,
        InstallBaseFacilityCommand{kWorkshop},
        CommandContext{profile.revision, "migration-resume-workshop"});
    ASSERT_TRUE(installed.succeeded) << installed.message;
    ASSERT_TRUE(profile.baseConstruction.activeProject.has_value());
    EXPECT_EQ(profile.baseConstruction.activeProject->completionWorldMinute,
              originalDeadline + 720U + 900U);
    ASSERT_TRUE(profile.baseManufacturing.activeOrder.has_value());
    EXPECT_EQ(profile.baseManufacturing.activeOrder->completionWorldMinute,
              originalManufacturingDeadline + 720U + 900U);
    EXPECT_EQ(projectBaseConstruction(profile, content).remainingMinutes, 720U);
    EXPECT_EQ(projectBaseManufacturing(profile).remainingMinutes, 360U);

    const std::uint64_t committed = profileStateFingerprint(profile);
    const BaseMigrationReceipt replay = executeBaseMigration(
        profile,
        content,
        BaseMigrationCommand{kAshworksSite},
        CommandContext{0U, "migration-greyline-ashworks"});
    EXPECT_TRUE(replay.succeeded);
    EXPECT_TRUE(replay.alreadyCommitted);
    EXPECT_EQ(profileStateFingerprint(profile), committed);
}

TEST(BaseMigrationDomainTest,
     ReserveFacilityReinstallsFreeAndReturnMigrationUsesSameTransaction)
{
    const ContentRegistry &content = publishedContentRegistry();
    ProfileState profile = makeNewAlphaProfile(
        "migration-return", content);
    prepareAshworks(profile);
    completeKitchenWater(profile);
    ASSERT_TRUE(executeBaseMigration(
        profile,
        content,
        BaseMigrationCommand{kAshworksSite},
        CommandContext{profile.revision, "migration-outbound"})
                    .succeeded);

    const std::uint64_t beforeInstallMinute =
        profile.worldClock.elapsedWorldMinutes;
    const InstallBaseFacilityReceipt installed = executeInstallBaseFacility(
        profile,
        content,
        InstallBaseFacilityCommand{kWorkshop},
        CommandContext{profile.revision, "migration-install-workshop"});
    ASSERT_TRUE(installed.succeeded) << installed.message;
    EXPECT_TRUE(baseFacilityInstalled(profile, kWorkshop));
    EXPECT_EQ(profile.worldClock.elapsedWorldMinutes, beforeInstallMinute);

    const BaseMigrationReceipt returned = executeBaseMigration(
        profile,
        content,
        BaseMigrationCommand{kGreylineSite},
        CommandContext{profile.revision, "migration-return-greyline"});
    ASSERT_TRUE(returned.succeeded) << returned.message;
    EXPECT_EQ(profile.regionalOperations.activeBaseNodeId,
              RegionNodeDefinitionId{"region_node.base.greyline_yard"});
    EXPECT_EQ(profile.regionalOperations.technologyCore.baseSiteDefinitionId,
              kGreylineSite);
    EXPECT_FALSE(profile.regionalOperations.outposts.at(kGreylineOutpost)
                     .established);
    EXPECT_TRUE(profile.regionalOperations.outposts.at(kAshworksOutpost)
                    .established);
    EXPECT_EQ(profile.baseConstruction.facilities.at(kWorkshop),
              BaseConstructionState::FacilityPlacement::Reserve);
    EXPECT_TRUE(validateProfileState(profile, content).valid);
}

TEST(BaseMigrationDomainTest,
     SiegeWarningBlocksMigrationAndReserveInstallationWithoutMutation)
{
    const ContentRegistry &content = publishedContentRegistry();
    ProfileState profile = makeNewAlphaProfile(
        "migration-siege-warning", content);
    prepareAshworks(profile);
    completeKitchenWater(profile);
    profile.baseConstruction.facilities[kWorkshop] =
        BaseConstructionState::FacilityPlacement::Reserve;
    profile.baseConstruction.facilityReserveStartedWorldMinutes[kWorkshop] =
        profile.worldClock.elapsedWorldMinutes;
    profile.baseSiege.raidThreatUnits = kBaseSiegeThreatThreshold;
    profile.baseSiege.safeUntilWorldMinute =
        profile.worldClock.elapsedWorldMinutes;
    ASSERT_TRUE(activateBaseSiegeWarningIfEligible(profile));
    const std::uint64_t before = profileStateFingerprint(profile);

    const BaseMigrationPlan migration = queryBaseMigration(
        profile, content, BaseMigrationCommand{kAshworksSite});
    EXPECT_FALSE(migration.canCommit);
    EXPECT_EQ(migration.error, DomainErrorCode::IllegalDestination);
    EXPECT_FALSE(executeBaseMigration(
        profile, content, BaseMigrationCommand{kAshworksSite},
        CommandContext{profile.revision, "migration-during-warning"})
                     .succeeded);
    EXPECT_EQ(profileStateFingerprint(profile), before);

    const InstallBaseFacilityPlan installation = queryInstallBaseFacility(
        profile, content, InstallBaseFacilityCommand{kWorkshop});
    EXPECT_FALSE(installation.canCommit);
    EXPECT_EQ(installation.error, DomainErrorCode::IllegalDestination);
    EXPECT_FALSE(executeInstallBaseFacility(
        profile, content, InstallBaseFacilityCommand{kWorkshop},
        CommandContext{profile.revision, "install-during-warning"})
                     .succeeded);
    EXPECT_EQ(profileStateFingerprint(profile), before);
}
