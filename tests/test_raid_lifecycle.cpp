#include <gtest/gtest.h>

#include "base_workforce_domain.h"

#include <algorithm>
#include <array>
#include <limits>
#include <map>
#include <set>
#include <tuple>

#include "alpha_content_ids.h"
#include "base_manufacturing_domain.h"
#include "base_morale_domain.h"
#include "base_resource_domain.h"
#include "base_siege_domain.h"
#include "raid_lifecycle.h"
#include "raid_rescue_domain.h"
#include "regional_operations_domain.h"

namespace
{
AssetInstanceId firstAsset(
    const ProfileState &profile,
    const ItemDefinitionId &definitionId)
{
    const auto found = std::find_if(
        profile.assets.records().begin(),
        profile.assets.records().end(),
        [&definitionId](const auto &entry)
        {
            return entry.second.definitionId == definitionId;
        });
    return found == profile.assets.records().end() ? 0 : found->first;
}

DeployReceipt deploy(
    ProfileState &profile,
    std::uint64_t seed = 9917,
    MapDefinitionId mapDefinitionId = MapDefinitionId{"map.v0.test"});

TEST(RaidLifecycleTest, SiegeWarningBlocksDeployWithoutMutation)
{
    ProfileState profile = makeNewAlphaProfile(
        "siege-blocks-deploy", publishedContentRegistry());
    profile.baseSiege.raidThreatUnits = kBaseSiegeThreatThreshold;
    profile.baseSiege.warningActive = true;
    profile.baseSiege.warningRemainingSeconds = kBaseSiegeWarningSeconds;
    const std::uint64_t before = profileStateFingerprint(profile);

    const DeployReceipt receipt = deploy(profile, 44001U);

    EXPECT_FALSE(receipt.succeeded);
    EXPECT_EQ(profileStateFingerprint(profile), before);
}

TEST(RaidLifecycleTest, QueuedSiegeDuringSafetyStillAllowsDeploy)
{
    ProfileState profile = makeNewAlphaProfile(
        "siege-safety-allows-deploy", publishedContentRegistry());
    profile.baseSiege.raidThreatUnits = kBaseSiegeThreatThreshold;
    ASSERT_TRUE(projectBaseThreat(profile).siegeQueued);

    const DeployReceipt receipt = deploy(profile, 44004U);

    EXPECT_TRUE(receipt.succeeded) << receipt.message;
    EXPECT_TRUE(profile.pendingRaid.has_value());
}

TEST(RaidLifecycleTest, SettlementAddsThreatOnceAndRollbackRestoresEntryState)
{
    ProfileState settled = makeNewAlphaProfile(
        "siege-settlement", publishedContentRegistry());
    ASSERT_TRUE(deploy(settled, 44002U).succeeded);
    const std::string settlementId = settled.pendingRaid->settlementId;
    ASSERT_TRUE(settlePendingRaid(
        settled,
        publishedContentRegistry(),
        settlementId,
        RaidResultOutcome::Extracted).succeeded);
    EXPECT_EQ(
        settled.baseSiege.raidThreatUnits,
        kBaseSiegeRaidThreatUnits);
    const std::uint64_t committed = profileStateFingerprint(settled);
    const RaidSettlementReceipt replay = settlePendingRaid(
        settled,
        publishedContentRegistry(),
        settlementId,
        RaidResultOutcome::Extracted);
    EXPECT_TRUE(replay.succeeded);
    EXPECT_TRUE(replay.alreadyCommitted);
    EXPECT_EQ(profileStateFingerprint(settled), committed);

    ProfileState rolledBack = makeNewAlphaProfile(
        "siege-rollback", publishedContentRegistry());
    rolledBack.baseSiege.raidThreatUnits = 37U;
    ASSERT_TRUE(deploy(rolledBack, 44003U).succeeded);
    rolledBack.baseSiege.raidThreatUnits = 99U;
    rolledBack.baseSiege.populationThreatUnits = 40U;
    ASSERT_TRUE(rollbackPendingRaidToBase(
        rolledBack, publishedContentRegistry()).succeeded);
    EXPECT_EQ(rolledBack.baseSiege.raidThreatUnits, 37U);
    EXPECT_EQ(rolledBack.baseSiege.populationThreatUnits, 0U);
}

TEST(RaidLifecycleTest,
     BasePerimeterSweepKeepsNormalExtractionOpenAndRequiresObjective)
{
    const ContentRegistry &content = publishedContentRegistry();
    const RegionalBaseSiteDefinitionId siteId{
        "regional_base_site.greyline_yard"};
    ProfileState profile = makeNewAlphaProfile(
        "perimeter-early-extraction", content);
    profile.baseSiege.raidThreatUnits = 40U;

    const DeployReceipt deployed = executeDeploy(
        profile,
        content,
        DeployCommand{
            "perimeter-early-raid",
            "perimeter-early-settlement",
            44010U,
            MapDefinitionId{"map.v0.test"},
            {},
            std::nullopt,
            std::nullopt,
            std::nullopt,
            siteId},
        CommandContext{profile.revision, "deploy:perimeter-early"});
    ASSERT_TRUE(deployed.succeeded) << deployed.message;
    ASSERT_TRUE(profile.pendingRaid->basePerimeterSweep.has_value());
    EXPECT_FALSE(profile.pendingRaid->basePerimeterSweep->objectiveSecured);

    const RaidSettlementReceipt settled = settlePendingRaid(
        profile,
        content,
        "perimeter-early-settlement",
        RaidResultOutcome::Extracted);

    ASSERT_TRUE(settled.succeeded) << settled.message;
    EXPECT_EQ(totalBaseThreat(profile.baseSiege), 60U);
    ASSERT_TRUE(profile.lastRaidResult.has_value());
    EXPECT_EQ(profile.lastRaidResult->baseThreatReducedUnits, 0U);
}

TEST(RaidLifecycleTest,
     SecuredBasePerimeterSweepReducesThreatOnceAtExtraction)
{
    const ContentRegistry &content = publishedContentRegistry();
    const RegionalBaseSiteDefinitionId siteId{
        "regional_base_site.greyline_yard"};
    ProfileState profile = makeNewAlphaProfile(
        "perimeter-success", content);
    profile.baseSiege.raidThreatUnits = 70U;
    ASSERT_TRUE(executeDeploy(
        profile,
        content,
        DeployCommand{
            "perimeter-success-raid",
            "perimeter-success-settlement",
            44011U,
            MapDefinitionId{"map.v0.test"},
            {},
            std::nullopt,
            std::nullopt,
            std::nullopt,
            siteId},
        CommandContext{profile.revision, "deploy:perimeter-success"})
                    .succeeded);
    profile.pendingRaid->basePerimeterSweep->objectiveSecured = true;

    const RaidSettlementReceipt settled = settlePendingRaid(
        profile,
        content,
        "perimeter-success-settlement",
        RaidResultOutcome::Extracted);

    ASSERT_TRUE(settled.succeeded) << settled.message;
    EXPECT_EQ(totalBaseThreat(profile.baseSiege), 50U);
    ASSERT_TRUE(profile.lastRaidResult.has_value());
    EXPECT_EQ(profile.lastRaidResult->baseThreatReducedUnits, 40U);
    const std::uint64_t committed = profileStateFingerprint(profile);
    const RaidSettlementReceipt replay = settlePendingRaid(
        profile,
        content,
        "perimeter-success-settlement",
        RaidResultOutcome::Extracted);
    EXPECT_TRUE(replay.succeeded);
    EXPECT_TRUE(replay.alreadyCommitted);
    EXPECT_EQ(profileStateFingerprint(profile), committed);
}

TEST(RaidLifecycleTest,
     FailedOrAbnormalBasePerimeterSweepDoesNotReduceThreat)
{
    const ContentRegistry &content = publishedContentRegistry();
    const RegionalBaseSiteDefinitionId siteId{
        "regional_base_site.greyline_yard"};
    ProfileState failed = makeNewAlphaProfile("perimeter-failed", content);
    failed.baseSiege.raidThreatUnits = 70U;
    ASSERT_TRUE(executeDeploy(
        failed,
        content,
        DeployCommand{
            "perimeter-failed-raid", "perimeter-failed-settlement",
            44012U, MapDefinitionId{"map.v0.test"}, {},
            std::nullopt, std::nullopt, std::nullopt, siteId},
        CommandContext{failed.revision, "deploy:perimeter-failed"})
                    .succeeded);
    failed.pendingRaid->basePerimeterSweep->objectiveSecured = true;
    ASSERT_TRUE(settlePendingRaid(
        failed,
        content,
        "perimeter-failed-settlement",
        RaidResultOutcome::PlayerDead).succeeded);
    EXPECT_EQ(totalBaseThreat(failed.baseSiege), 90U);
    EXPECT_EQ(failed.lastRaidResult->baseThreatReducedUnits, 0U);

    ProfileState rollback = makeNewAlphaProfile(
        "perimeter-rollback", content);
    rollback.baseSiege.raidThreatUnits = 70U;
    ASSERT_TRUE(executeDeploy(
        rollback,
        content,
        DeployCommand{
            "perimeter-rollback-raid", "perimeter-rollback-settlement",
            44013U, MapDefinitionId{"map.v0.test"}, {},
            std::nullopt, std::nullopt, std::nullopt, siteId},
        CommandContext{rollback.revision, "deploy:perimeter-rollback"})
                    .succeeded);
    rollback.pendingRaid->basePerimeterSweep->objectiveSecured = true;
    ASSERT_TRUE(rollbackPendingRaidToBase(rollback, content).succeeded);
    EXPECT_EQ(totalBaseThreat(rollback.baseSiege), 70U);
    EXPECT_FALSE(rollback.lastRaidResult.has_value());
}

void equipAlphaLoadout(ProfileState &profile)
{
    for (const auto &[definitionId, slot, transaction] :
         std::vector<std::tuple<ItemDefinitionId, EquipmentSlotKind, std::string>>{
             {alpha_content::rifle, EquipmentSlotKind::PrimaryWeapon, "equip-rifle"},
             {alpha_content::pistol, EquipmentSlotKind::Sidearm, "equip-pistol"},
             {alpha_content::helmet, EquipmentSlotKind::Helmet, "equip-helmet"},
             {alpha_content::bodyArmor, EquipmentSlotKind::BodyArmor, "equip-body-armor"},
             {alpha_content::chestRig, EquipmentSlotKind::ChestRig, "equip-chest"},
             {alpha_content::backpack, EquipmentSlotKind::Backpack, "equip-backpack"}})
    {
        const InventoryReceipt receipt = executeInventory(
            profile,
            publishedContentRegistry(),
            InventoryEquipCommand{firstAsset(profile, definitionId), slot},
            CommandContext{profile.revision, transaction});
        ASSERT_TRUE(receipt.succeeded) << receipt.message;
    }
}

DeployReceipt deploy(
    ProfileState &profile,
    std::uint64_t seed,
    MapDefinitionId mapDefinitionId)
{
    return executeDeploy(
        profile,
        publishedContentRegistry(),
        DeployCommand{
            "raid-alpha-test",
            "settlement-alpha-test",
            seed,
            std::move(mapDefinitionId)},
        CommandContext{profile.revision, "deploy-alpha-test"});
}

TEST(RaidLifecycleTest, TravelPreviewUsesMapTimeWithoutMutatingProfile)
{
    const ProfileState profile = makeNewAlphaProfile(
        "travel-preview", publishedContentRegistry());
    const std::uint64_t fingerprint = profileStateFingerprint(profile);
    const MapDefinition &map = publishedContentRegistry().map(
        MapDefinitionId{"map.raid.industrial"});

    const RaidTravelPreview preview = queryRaidTravel(
        profile, publishedContentRegistry(), map);

    EXPECT_EQ(preview.outboundMinutes, 150U);
    EXPECT_EQ(preview.returnMinutes, 150U);
    EXPECT_EQ(preview.failureRegroupMinutes, 300U);
    EXPECT_EQ(preview.departure.hour, 8U);
    EXPECT_EQ(preview.arrival.hour, 10U);
    EXPECT_EQ(preview.arrival.minute, 30U);
    EXPECT_EQ(preview.extractedReturn.hour, 13U);
    EXPECT_EQ(preview.failureReturn.hour, 15U);
    EXPECT_EQ(preview.failureReturn.minute, 30U);
    EXPECT_EQ(profileStateFingerprint(profile), fingerprint);
}

TEST(RaidLifecycleTest, DeployFreezesAndAppliesOutboundTravel)
{
    ProfileState profile = makeNewAlphaProfile(
        "travel-deploy", publishedContentRegistry());
    const WorldClockState startingClock = profile.worldClock;
    const BaseResourceState startingResources = profile.baseResources;
    const BasePriorityState startingPriority = profile.basePriority;
    const BaseMoraleState startingMorale = profile.baseMorale;
    const BaseCommunityEventState startingEvent =
        profile.baseCommunityEvent;

    ASSERT_TRUE(deploy(
        profile, 77230, MapDefinitionId{"map.raid.riverside"}).succeeded);
    ASSERT_TRUE(profile.pendingRaid.has_value());
    EXPECT_EQ(
        profile.pendingRaid->rulesVersion,
        "procedural-playable-outdoor-layout-21");
    ASSERT_TRUE(profile.pendingRaid->rescue.has_value());
    EXPECT_EQ(
        profile.pendingRaid->rescue->definitionId,
        RescueDefinitionId{"rescue.ordinary.riverside_checkpoint"});
    EXPECT_FALSE(profile.pendingRaid->rescue->secured);
    EXPECT_EQ(profile.pendingRaid->travel.outboundMinutes, 90U);
    EXPECT_EQ(profile.pendingRaid->travel.returnMinutes, 90U);
    EXPECT_EQ(profile.pendingRaid->travel.failureRegroupMinutes, 180U);
    ASSERT_EQ(profile.pendingRaid->travel.routeIds.size(), 1U);
    EXPECT_EQ(
        profile.pendingRaid->travel.routeIds.front(),
        RegionRouteDefinitionId{"region_route.riverside_direct"});
    EXPECT_EQ(
        profile.pendingRaid->travel.startingRegionalOperations,
        profile.regionalOperations);
    EXPECT_EQ(profile.pendingRaid->travel.startingWorldClock, startingClock);
    EXPECT_EQ(profile.pendingRaid->travel.startingBaseResources,
              startingResources);
    EXPECT_EQ(profile.pendingRaid->travel.startingBasePriority,
              startingPriority);
    EXPECT_EQ(profile.pendingRaid->travel.startingBaseMorale,
              startingMorale);
    EXPECT_EQ(profile.pendingRaid->travel.startingBaseCommunityEvent,
              startingEvent);
    EXPECT_EQ(profile.worldClock.elapsedWorldMinutes,
              startingClock.elapsedWorldMinutes + 90U);

    ASSERT_TRUE(rollbackPendingRaidToBase(
        profile, publishedContentRegistry()).succeeded);
    EXPECT_EQ(profile.worldClock, startingClock);
    EXPECT_EQ(profile.baseResources, startingResources);
    EXPECT_EQ(profile.basePriority, startingPriority);
    EXPECT_EQ(profile.baseMorale, startingMorale);
    EXPECT_EQ(profile.baseCommunityEvent, startingEvent);
}

TEST(RaidLifecycleTest,
     OnlineOutpostRouteIsFrozenAndStaffingCannotChangeDuringRaid)
{
    const ContentRegistry &content = publishedContentRegistry();
    ProfileState profile = makeNewAlphaProfile(
        "travel-online-outpost", content);
    const RegionalOutpostDefinitionId outpostId{
        "regional_outpost.old_service_relay"};
    ASSERT_TRUE(executeEstablishRegionalOutpost(
        profile,
        content,
        EstablishRegionalOutpostCommand{outpostId},
        CommandContext{profile.revision, "travel-establish-outpost"})
                    .succeeded);
    ASSERT_TRUE(executeRegionalOutpostStaffing(
        profile,
        content,
        RegionalOutpostStaffingCommand{outpostId, true},
        CommandContext{profile.revision, "travel-staff-outpost"})
                    .succeeded);
    const std::uint64_t startingMinute =
        profile.worldClock.elapsedWorldMinutes;

    ASSERT_TRUE(deploy(
        profile, 17001U,
        MapDefinitionId{"map.raid.industrial"}).succeeded);
    ASSERT_TRUE(profile.pendingRaid.has_value());
    EXPECT_EQ(profile.pendingRaid->travel.outboundMinutes, 95U);
    EXPECT_EQ(profile.pendingRaid->travel.returnMinutes, 95U);
    EXPECT_EQ(profile.pendingRaid->travel.failureRegroupMinutes, 190U);
    ASSERT_EQ(profile.pendingRaid->travel.routeIds.size(), 2U);
    const std::uint64_t pendingFingerprint =
        profileStateFingerprint(profile);

    const RegionalOutpostStaffingReceipt blocked =
        executeRegionalOutpostStaffing(
            profile,
            content,
            RegionalOutpostStaffingCommand{outpostId, false},
            CommandContext{profile.revision, "travel-clear-pending"});
    EXPECT_FALSE(blocked.succeeded);
    EXPECT_EQ(profileStateFingerprint(profile), pendingFingerprint);

    const RaidSettlementReceipt settled = settlePendingRaid(
        profile,
        content,
        "settlement-alpha-test",
        RaidResultOutcome::Extracted);
    ASSERT_TRUE(settled.succeeded) << settled.message;
    EXPECT_EQ(
        profile.worldClock.elapsedWorldMinutes,
        startingMinute + 190U);
}

TEST(RaidLifecycleTest,
     ThreeSettledShortcutRaidsDisruptOnlyAfterFrozenReturn)
{
    const ContentRegistry &content = publishedContentRegistry();
    ProfileState profile = makeNewAlphaProfile(
        "travel-outpost-disruption", content);
    const RegionalOutpostDefinitionId outpostId{
        "regional_outpost.old_service_relay"};
    ASSERT_TRUE(executeEstablishRegionalOutpost(
        profile,
        content,
        EstablishRegionalOutpostCommand{outpostId},
        CommandContext{profile.revision, "threat-establish"})
                    .succeeded);
    ASSERT_TRUE(executeRegionalOutpostStaffing(
        profile,
        content,
        RegionalOutpostStaffingCommand{outpostId, true},
        CommandContext{profile.revision, "threat-staff"})
                    .succeeded);

    for (std::uint32_t index{}; index < 3U; ++index)
    {
        const std::string raidId = "threat-raid-" +
            std::to_string(index);
        const std::string settlementId = "threat-settlement-" +
            std::to_string(index);
        const DeployReceipt deployed = executeDeploy(
            profile,
            content,
            DeployCommand{
                raidId,
                settlementId,
                81000U + index,
                MapDefinitionId{"map.raid.industrial"}},
            CommandContext{
                profile.revision,
                "deploy:" + raidId});
        ASSERT_TRUE(deployed.succeeded) << deployed.message;
        ASSERT_TRUE(profile.pendingRaid.has_value());
        EXPECT_EQ(profile.pendingRaid->travel.outboundMinutes, 95U);
        EXPECT_EQ(
            profile.regionalOperations.outposts.at(outpostId)
                .shortcutOperationsSinceRestoration,
            index);

        const RaidSettlementReceipt settled = settlePendingRaid(
            profile,
            content,
            settlementId,
            RaidResultOutcome::Extracted);
        ASSERT_TRUE(settled.succeeded) << settled.message;
        const RegionalOutpostState &state =
            profile.regionalOperations.outposts.at(outpostId);
        EXPECT_EQ(
            state.shortcutOperationsSinceRestoration,
            index + 1U);
        EXPECT_EQ(state.disrupted, index == 2U);

        const std::uint64_t fingerprint = profileStateFingerprint(profile);
        const RaidSettlementReceipt replay = settlePendingRaid(
            profile,
            content,
            settlementId,
            RaidResultOutcome::Extracted);
        ASSERT_TRUE(replay.succeeded);
        EXPECT_TRUE(replay.alreadyCommitted);
        EXPECT_EQ(profileStateFingerprint(profile), fingerprint);
    }

    const RegionalRoutePlan direct = queryRegionalRoute(
        profile,
        content,
        MapDefinitionId{"map.raid.industrial"});
    ASSERT_TRUE(direct.reachable);
    EXPECT_FALSE(direct.usesOnlineOutpost);
    EXPECT_EQ(direct.travelMinutes, 150U);
}

TEST(RaidLifecycleTest,
     DirectRouteAndAbnormalRollbackDoNotAdvanceOutpostThreat)
{
    const ContentRegistry &content = publishedContentRegistry();
    ProfileState profile = makeNewAlphaProfile(
        "travel-outpost-no-threat", content);
    const RegionalOutpostDefinitionId outpostId{
        "regional_outpost.old_service_relay"};

    ASSERT_TRUE(deploy(
        profile,
        82001U,
        MapDefinitionId{"map.raid.industrial"}).succeeded);
    ASSERT_TRUE(settlePendingRaid(
        profile,
        content,
        "settlement-alpha-test",
        RaidResultOutcome::Extracted).succeeded);
    EXPECT_EQ(
        profile.regionalOperations.outposts.at(outpostId)
            .shortcutOperationsSinceRestoration,
        0U);

    ASSERT_TRUE(executeEstablishRegionalOutpost(
        profile,
        content,
        EstablishRegionalOutpostCommand{outpostId},
        CommandContext{profile.revision, "rollback-establish"})
                    .succeeded);
    ASSERT_TRUE(executeRegionalOutpostStaffing(
        profile,
        content,
        RegionalOutpostStaffingCommand{outpostId, true},
        CommandContext{profile.revision, "rollback-staff"})
                    .succeeded);
    ASSERT_TRUE(executeDeploy(
        profile,
        content,
        DeployCommand{
            "rollback-threat-raid",
            "rollback-threat-settlement",
            82002U,
            MapDefinitionId{"map.raid.industrial"}},
        CommandContext{profile.revision, "deploy:rollback-threat"})
                    .succeeded);
    ASSERT_TRUE(rollbackPendingRaidToBase(profile, content).succeeded);
    EXPECT_EQ(
        profile.regionalOperations.outposts.at(outpostId)
            .shortcutOperationsSinceRestoration,
        0U);
}

TEST(RaidLifecycleTest,
     RestorationOnlyCommitsAfterClearingButEarlyExtractionRemainsValid)
{
    const ContentRegistry &content = publishedContentRegistry();
    ProfileState profile = makeNewAlphaProfile(
        "outpost-restoration-success", content);
    const RegionalOutpostDefinitionId outpostId{
        "regional_outpost.old_service_relay"};
    ASSERT_TRUE(executeEstablishRegionalOutpost(
        profile, content, EstablishRegionalOutpostCommand{outpostId},
        CommandContext{profile.revision, "restore-establish"}).succeeded);
    ASSERT_TRUE(executeRegionalOutpostStaffing(
        profile, content, RegionalOutpostStaffingCommand{outpostId, true},
        CommandContext{profile.revision, "restore-staff"}).succeeded);
    RegionalOutpostState &outpost =
        profile.regionalOperations.outposts.at(outpostId);
    outpost.shortcutOperationsSinceRestoration = 3U;
    outpost.disrupted = true;

    const DeployReceipt deployed = executeDeploy(
        profile,
        content,
        DeployCommand{
            "restoration-raid",
            "restoration-settlement",
            83001U,
            MapDefinitionId{"map.raid.riverside"},
            {},
            std::nullopt,
            outpostId},
        CommandContext{profile.revision, "deploy:restoration"});
    ASSERT_TRUE(deployed.succeeded) << deployed.message;
    ASSERT_TRUE(profile.pendingRaid->outpostRestoration.has_value());
    EXPECT_FALSE(profile.pendingRaid->outpostRestoration->objectiveSecured);

    ProfileState earlyExtraction = profile;
    const RaidSettlementReceipt early = settlePendingRaid(
        earlyExtraction,
        content,
        "restoration-settlement",
        RaidResultOutcome::Extracted);
    ASSERT_TRUE(early.succeeded) << early.message;
    EXPECT_FALSE(earlyExtraction.pendingRaid.has_value());
    EXPECT_TRUE(earlyExtraction.regionalOperations.outposts.at(outpostId)
                    .disrupted);
    EXPECT_EQ(
        earlyExtraction.regionalOperations.outposts.at(outpostId)
            .shortcutOperationsSinceRestoration,
        3U);

    profile.pendingRaid->outpostRestoration->objectiveSecured = true;
    const RaidSettlementReceipt settled = settlePendingRaid(
        profile,
        content,
        "restoration-settlement",
        RaidResultOutcome::Extracted);
    ASSERT_TRUE(settled.succeeded) << settled.message;
    const RegionalOutpostState &restored =
        profile.regionalOperations.outposts.at(outpostId);
    EXPECT_FALSE(restored.disrupted);
    EXPECT_EQ(restored.shortcutOperationsSinceRestoration, 0U);
    EXPECT_TRUE(regionalOutpostOnline(profile, content, outpostId));
}

TEST(RaidLifecycleTest,
     FailedOrAbnormalRestorationKeepsDisruption)
{
    const ContentRegistry &content = publishedContentRegistry();
    const RegionalOutpostDefinitionId outpostId{
        "regional_outpost.old_service_relay"};
    for (const RaidResultOutcome outcome : {
             RaidResultOutcome::PlayerDead,
             RaidResultOutcome::ActiveQuit})
    {
        ProfileState profile = makeNewAlphaProfile(
            outcome == RaidResultOutcome::PlayerDead
                ? "outpost-restoration-death"
                : "outpost-restoration-quit",
            content);
        ASSERT_TRUE(executeEstablishRegionalOutpost(
            profile, content, EstablishRegionalOutpostCommand{outpostId},
            CommandContext{profile.revision, "failed-restore-establish"})
                        .succeeded);
        ASSERT_TRUE(executeRegionalOutpostStaffing(
            profile, content,
            RegionalOutpostStaffingCommand{outpostId, true},
            CommandContext{profile.revision, "failed-restore-staff"})
                        .succeeded);
        RegionalOutpostState &outpost =
            profile.regionalOperations.outposts.at(outpostId);
        outpost.shortcutOperationsSinceRestoration = 3U;
        outpost.disrupted = true;
        ASSERT_TRUE(executeDeploy(
            profile,
            content,
            DeployCommand{
                "failed-restoration-raid",
                "failed-restoration-settlement",
                83002U,
                MapDefinitionId{"map.raid.riverside"},
                {},
                std::nullopt,
                outpostId},
            CommandContext{profile.revision, "deploy:failed-restoration"})
                        .succeeded);
        profile.pendingRaid->outpostRestoration->objectiveSecured = true;
        ASSERT_TRUE(settlePendingRaid(
            profile,
            content,
            "failed-restoration-settlement",
            outcome).succeeded);
        const RegionalOutpostState &stillDisrupted =
            profile.regionalOperations.outposts.at(outpostId);
        EXPECT_TRUE(stillDisrupted.disrupted);
        EXPECT_EQ(
            stillDisrupted.shortcutOperationsSinceRestoration,
            3U);
    }

    ProfileState rollback = makeNewAlphaProfile(
        "outpost-restoration-rollback", content);
    ASSERT_TRUE(executeEstablishRegionalOutpost(
        rollback, content, EstablishRegionalOutpostCommand{outpostId},
        CommandContext{rollback.revision, "rollback-restore-establish"})
                    .succeeded);
    ASSERT_TRUE(executeRegionalOutpostStaffing(
        rollback, content, RegionalOutpostStaffingCommand{outpostId, true},
        CommandContext{rollback.revision, "rollback-restore-staff"})
                    .succeeded);
    RegionalOutpostState &rollbackOutpost =
        rollback.regionalOperations.outposts.at(outpostId);
    rollbackOutpost.shortcutOperationsSinceRestoration = 3U;
    rollbackOutpost.disrupted = true;
    const RegionalOperationsState startingRegional =
        rollback.regionalOperations;
    ASSERT_TRUE(executeDeploy(
        rollback,
        content,
        DeployCommand{
            "rollback-restoration-raid",
            "rollback-restoration-settlement",
            83003U,
            MapDefinitionId{"map.raid.riverside"},
            {},
            std::nullopt,
            outpostId},
        CommandContext{rollback.revision, "deploy:rollback-restoration"})
                    .succeeded);
    ASSERT_TRUE(rollbackPendingRaidToBase(rollback, content).succeeded);
    EXPECT_EQ(rollback.regionalOperations, startingRegional);
}

TEST(RaidLifecycleTest,
     BaseSiteClearanceOnlyUnlocksAfterObjectiveButEarlyExtractionIsValid)
{
    const ContentRegistry &content = publishedContentRegistry();
    ProfileState profile = makeNewAlphaProfile(
        "base-site-clearance-success", content);
    const RegionalBaseSiteDefinitionId siteId{
        "regional_base_site.ashworks_logistics_yard"};
    const RegionalOutpostDefinitionId outpostId{
        "regional_outpost.ashworks_logistics_yard"};

    const DeployReceipt deployed = executeDeploy(
        profile,
        content,
        DeployCommand{
            "base-site-clearance-raid",
            "base-site-clearance-settlement",
            84001U,
            MapDefinitionId{"map.raid.industrial"},
            {},
            std::nullopt,
            std::nullopt,
            siteId},
        CommandContext{profile.revision, "deploy:base-site-clearance"});
    ASSERT_TRUE(deployed.succeeded) << deployed.message;
    ASSERT_TRUE(profile.pendingRaid->baseSiteClearance.has_value());
    EXPECT_EQ(
        profile.pendingRaid->rulesVersion,
        "procedural-playable-outdoor-layout-21");
    EXPECT_FALSE(profile.pendingRaid->baseSiteClearance->objectiveSecured);

    ProfileState earlyExtraction = profile;
    const RaidSettlementReceipt early = settlePendingRaid(
        earlyExtraction,
        content,
        "base-site-clearance-settlement",
        RaidResultOutcome::Extracted);
    ASSERT_TRUE(early.succeeded) << early.message;
    EXPECT_FALSE(earlyExtraction.pendingRaid.has_value());
    EXPECT_FALSE(
        earlyExtraction.regionalOperations.baseSites.at(siteId).unlocked);
    EXPECT_FALSE(
        earlyExtraction.regionalOperations.outposts.at(outpostId).unlocked);

    profile.pendingRaid->baseSiteClearance->objectiveSecured = true;
    const RaidSettlementReceipt settled = settlePendingRaid(
        profile,
        content,
        "base-site-clearance-settlement",
        RaidResultOutcome::Extracted);
    ASSERT_TRUE(settled.succeeded) << settled.message;
    EXPECT_TRUE(profile.regionalOperations.baseSites.at(siteId).unlocked);
    EXPECT_TRUE(profile.regionalOperations.outposts.at(outpostId).unlocked);
    EXPECT_FALSE(profile.regionalOperations.outposts.at(outpostId).established);

    const ProfileRevision settledRevision = profile.revision;
    const std::uint64_t settledFingerprint =
        profileStateFingerprint(profile);
    const RaidSettlementReceipt replay = settlePendingRaid(
        profile,
        content,
        "base-site-clearance-settlement",
        RaidResultOutcome::Extracted);
    EXPECT_TRUE(replay.succeeded);
    EXPECT_TRUE(replay.alreadyCommitted);
    EXPECT_EQ(profile.revision, settledRevision);
    EXPECT_EQ(profileStateFingerprint(profile), settledFingerprint);

    ASSERT_TRUE(executeEstablishRegionalOutpost(
        profile, content, EstablishRegionalOutpostCommand{outpostId},
        CommandContext{profile.revision, "establish:ashworks-yard"})
                    .succeeded);
    ASSERT_TRUE(executeRegionalOutpostStaffing(
        profile, content, RegionalOutpostStaffingCommand{outpostId, true},
        CommandContext{profile.revision, "staff:ashworks-yard"})
                    .succeeded);
    const RegionalRoutePlan industrial = queryRegionalRoute(
        profile, content, MapDefinitionId{"map.raid.industrial"});
    ASSERT_TRUE(industrial.reachable);
    EXPECT_TRUE(industrial.usesOnlineOutpost);
    EXPECT_EQ(industrial.travelMinutes, 75U);
    const RegionalRoutePlan frontier = queryRegionalRoute(
        profile, content, MapDefinitionId{"map.raid.frontier_exchange"});
    ASSERT_TRUE(frontier.reachable);
    EXPECT_TRUE(frontier.usesOnlineOutpost);
    EXPECT_EQ(frontier.travelMinutes, 115U);
}

TEST(RaidLifecycleTest,
     FailedOrAbnormalBaseSiteClearanceLeavesSiteAndOutpostLocked)
{
    const ContentRegistry &content = publishedContentRegistry();
    const RegionalBaseSiteDefinitionId siteId{
        "regional_base_site.ashworks_logistics_yard"};
    const RegionalOutpostDefinitionId outpostId{
        "regional_outpost.ashworks_logistics_yard"};
    for (const RaidResultOutcome outcome : {
             RaidResultOutcome::PlayerDead,
             RaidResultOutcome::ActiveQuit})
    {
        ProfileState profile = makeNewAlphaProfile(
            outcome == RaidResultOutcome::PlayerDead
                ? "base-site-clearance-death"
                : "base-site-clearance-quit",
            content);
        ASSERT_TRUE(executeDeploy(
            profile,
            content,
            DeployCommand{
                "failed-base-site-clearance-raid",
                "failed-base-site-clearance-settlement",
                84002U,
                MapDefinitionId{"map.raid.industrial"},
                {},
                std::nullopt,
                std::nullopt,
                siteId},
            CommandContext{profile.revision,
                           "deploy:failed-base-site-clearance"})
                        .succeeded);
        profile.pendingRaid->baseSiteClearance->objectiveSecured = true;
        ASSERT_TRUE(settlePendingRaid(
            profile,
            content,
            "failed-base-site-clearance-settlement",
            outcome).succeeded);
        EXPECT_FALSE(profile.regionalOperations.baseSites.at(siteId).unlocked);
        EXPECT_FALSE(profile.regionalOperations.outposts.at(outpostId).unlocked);
    }

    ProfileState rollback = makeNewAlphaProfile(
        "base-site-clearance-rollback", content);
    const RegionalOperationsState startingRegional =
        rollback.regionalOperations;
    ASSERT_TRUE(executeDeploy(
        rollback,
        content,
        DeployCommand{
            "rollback-base-site-clearance-raid",
            "rollback-base-site-clearance-settlement",
            84003U,
            MapDefinitionId{"map.raid.industrial"},
            {},
            std::nullopt,
            std::nullopt,
            siteId},
        CommandContext{rollback.revision,
                       "deploy:rollback-base-site-clearance"})
                    .succeeded);
    rollback.pendingRaid->baseSiteClearance->objectiveSecured = true;
    ASSERT_TRUE(rollbackPendingRaidToBase(rollback, content).succeeded);
    EXPECT_EQ(rollback.regionalOperations, startingRegional);
}

TEST(RaidLifecycleTest, FrontierDeployFreezesIndependentInteriorActorsAndLoot)
{
    ProfileState profile = makeNewAlphaProfile(
        "interior-snapshot", publishedContentRegistry());
    const DeployReceipt receipt = deploy(
        profile,
        88123U,
        MapDefinitionId{"map.raid.frontier_exchange"});
    ASSERT_TRUE(receipt.succeeded) << receipt.message;
    ASSERT_TRUE(profile.pendingRaid.has_value());
    const PendingRaidSnapshot &raid = *profile.pendingRaid;
    const MapDefinition &map = publishedContentRegistry().map(
        MapDefinitionId{"map.raid.frontier_exchange"});
    ASSERT_EQ(raid.interiors.size(), 2U);
    ASSERT_EQ(raid.interiors.size(), map.interiors.size());
    for (std::size_t index{}; index < map.interiors.size(); ++index)
    {
        const RaidInteriorDefinition &definition = map.interiors[index];
        const RaidInteriorSnapshot &snapshot = raid.interiors[index];
        EXPECT_EQ(snapshot.id, definition.id);
        EXPECT_FALSE(snapshot.layoutKnown);
        const RaidAnchorPlacementSnapshot *placement =
            findRaidAnchorPlacement(
                raid.spatialLayout,
                raidIndexedAnchorId("interior", index));
        ASSERT_NE(placement, nullptr);
        EXPECT_EQ(placement->kind, RaidMapAnchorKind::InteriorEntrance);
        EXPECT_EQ(placement->bounds, snapshot.exteriorEntrance);
        EXPECT_EQ(
            std::count_if(
                raid.enemies.begin(), raid.enemies.end(),
                [&](const RaidEnemySnapshot &enemy)
                { return enemy.spaceId == definition.id; }),
            static_cast<std::ptrdiff_t>(definition.enemies.size()));
        EXPECT_EQ(
            std::count_if(
                raid.loot.begin(), raid.loot.end(),
                [&](const RaidLootSnapshot &loot)
                { return loot.spaceId == definition.id; }),
            static_cast<std::ptrdiff_t>(definition.lootSlots.size()));
        EXPECT_TRUE(std::none_of(
            raid.spatialLayout.ballisticBlockers.begin(),
            raid.spatialLayout.ballisticBlockers.end(),
            [&](const ContentRect &blocker)
            {
                const ContentRect entrance = snapshot.exteriorEntrance;
                return blocker.position.x <
                        entrance.position.x + entrance.size.x &&
                    blocker.position.x + blocker.size.x > entrance.position.x &&
                    blocker.position.y <
                        entrance.position.y + entrance.size.y &&
                    blocker.position.y + blocker.size.y > entrance.position.y;
            }));
    }
}

TEST(RaidLifecycleTest,
     FrontierDeployFreezesPermanentInteriorLayoutKnowledgeWithoutConsumingIt)
{
    const RaidSpaceDefinitionId interiorId{
        "raid_space.frontier_exchange.freight_service_bay"};
    ProfileState profile = makeNewAlphaProfile(
        "interior-layout-known", publishedContentRegistry());
    profile.raidInteriorIntelligence.knownLayouts.insert(interiorId);

    ASSERT_TRUE(deploy(
        profile,
        88124U,
        MapDefinitionId{"map.raid.frontier_exchange"}).succeeded);
    ASSERT_TRUE(profile.pendingRaid.has_value());
    ASSERT_EQ(profile.pendingRaid->interiors.size(), 2U);
    EXPECT_FALSE(profile.pendingRaid->interiors.front().layoutKnown);
    EXPECT_TRUE(profile.pendingRaid->interiors[1].layoutKnown);
    EXPECT_TRUE(profile.raidInteriorIntelligence.knows(interiorId));

    ASSERT_TRUE(rollbackPendingRaidToBase(
        profile, publishedContentRegistry()).succeeded);
    EXPECT_TRUE(profile.raidInteriorIntelligence.knows(interiorId));
}

TEST(RaidLifecycleTest, SpecialLocationPlacementIsStableAndVariesByRaidSeed)
{
    const MapDefinitionId mapId{"map.raid.frontier_exchange"};
    ProfileState first = makeNewAlphaProfile(
        "special-location-repeat-a", publishedContentRegistry());
    ProfileState repeated = makeNewAlphaProfile(
        "special-location-repeat-b", publishedContentRegistry());
    ASSERT_TRUE(deploy(first, 44119U, mapId).succeeded);
    ASSERT_TRUE(deploy(repeated, 44119U, mapId).succeeded);
    ASSERT_TRUE(first.pendingRaid.has_value());
    ASSERT_TRUE(repeated.pendingRaid.has_value());
    ASSERT_EQ(first.pendingRaid->interiors.size(), 2U);
    ASSERT_EQ(repeated.pendingRaid->interiors.size(), 2U);
    EXPECT_EQ(first.pendingRaid->interiors, repeated.pendingRaid->interiors);

    std::array<std::set<std::pair<int, int>>, 2> selectedPositions;
    for (std::uint64_t seed = 1U; seed <= 48U; ++seed)
    {
        ProfileState candidate = makeNewAlphaProfile(
            "special-location-variety", publishedContentRegistry());
        ASSERT_TRUE(deploy(candidate, seed, mapId).succeeded);
        ASSERT_TRUE(candidate.pendingRaid.has_value());
        ASSERT_EQ(candidate.pendingRaid->interiors.size(), 2U);
        for (std::size_t index{}; index < 2U; ++index)
        {
            const Vec2 position = candidate.pendingRaid->interiors[index]
                                      .exteriorEntrance.position;
            selectedPositions[index].emplace(
                static_cast<int>(position.x),
                static_cast<int>(position.y));
        }
    }
    EXPECT_GT(selectedPositions[0].size(), 1U);
    EXPECT_GT(selectedPositions[1].size(), 1U);
}

TEST(RaidLifecycleTest, DeployConsumesOnlySelectedMapIntelligence)
{
    ProfileState profile = makeNewAlphaProfile(
        "intelligence-deploy", publishedContentRegistry());
    const MapDefinitionId selectedMap{"map.raid.riverside"};
    const MapDefinitionId otherMap{"map.v0.test"};
    auto &selectedCounts = profile.raidIntelligence.counts[selectedMap];
    selectedCounts[raidIntelligenceCategoryIndex(
        RaidIntelligenceCategory::Transport)] = 2U;
    selectedCounts[raidIntelligenceCategoryIndex(
        RaidIntelligenceCategory::Enemy)] = 1U;
    profile.raidIntelligence.counts[otherMap][raidIntelligenceCategoryIndex(
        RaidIntelligenceCategory::Transport)] = 3U;
    RaidIntelligenceLoadout loadout;
    loadout.set(RaidIntelligenceCategory::Transport, true);
    loadout.set(RaidIntelligenceCategory::Enemy, true);

    const DeployReceipt receipt = executeDeploy(
        profile,
        publishedContentRegistry(),
        DeployCommand{"raid-with-intelligence", "settle-with-intelligence",
                      77331U, selectedMap, loadout},
        {profile.revision, "deploy-with-intelligence"});

    ASSERT_TRUE(receipt.succeeded) << receipt.message;
    ASSERT_TRUE(profile.pendingRaid.has_value());
    EXPECT_EQ(profile.pendingRaid->intelligence, loadout);
    EXPECT_EQ(profile.raidIntelligence.count(
        selectedMap, RaidIntelligenceCategory::Transport), 1U);
    EXPECT_EQ(profile.raidIntelligence.count(
        selectedMap, RaidIntelligenceCategory::Enemy), 0U);
    EXPECT_EQ(profile.raidIntelligence.count(
        otherMap, RaidIntelligenceCategory::Transport), 3U);
    EXPECT_EQ(profile.pendingRaid->travel.startingRaidIntelligence.count(
        selectedMap, RaidIntelligenceCategory::Transport), 2U);
}

TEST(RaidLifecycleTest, MissingSelectedIntelligenceRejectsWithoutMutation)
{
    ProfileState profile = makeNewAlphaProfile(
        "intelligence-missing", publishedContentRegistry());
    RaidIntelligenceLoadout loadout;
    loadout.set(RaidIntelligenceCategory::Resource, true);
    const std::uint64_t fingerprint = profileStateFingerprint(profile);

    const DeployReceipt receipt = executeDeploy(
        profile,
        publishedContentRegistry(),
        DeployCommand{"raid-missing-intelligence",
                      "settle-missing-intelligence", 77332U,
                      MapDefinitionId{"map.v0.test"}, loadout},
        {profile.revision, "deploy-missing-intelligence"});

    EXPECT_FALSE(receipt.succeeded);
    EXPECT_EQ(receipt.error, RaidLifecycleError::InsufficientIntelligence);
    EXPECT_EQ(profileStateFingerprint(profile), fingerprint);
}

TEST(RaidLifecycleTest, AbnormalRollbackRestoresConsumedIntelligence)
{
    ProfileState profile = makeNewAlphaProfile(
        "intelligence-rollback", publishedContentRegistry());
    const MapDefinitionId mapId{"map.v0.test"};
    profile.raidIntelligence.counts[mapId][raidIntelligenceCategoryIndex(
        RaidIntelligenceCategory::Resource)] = 1U;
    const RaidIntelligenceArchiveState starting = profile.raidIntelligence;
    RaidIntelligenceLoadout loadout;
    loadout.set(RaidIntelligenceCategory::Resource, true);
    ASSERT_TRUE(executeDeploy(
        profile,
        publishedContentRegistry(),
        DeployCommand{"raid-intelligence-rollback",
                      "settle-intelligence-rollback", 77333U, mapId, loadout},
        {profile.revision, "deploy-intelligence-rollback"}).succeeded);
    EXPECT_EQ(profile.raidIntelligence.count(
        mapId, RaidIntelligenceCategory::Resource), 0U);

    const RaidRollbackReceipt receipt = rollbackPendingRaidToBase(
        profile, publishedContentRegistry());

    ASSERT_TRUE(receipt.succeeded) << receipt.message;
    EXPECT_EQ(profile.raidIntelligence, starting);
    EXPECT_FALSE(profile.pendingRaid.has_value());
}

TEST(RaidLifecycleTest, OutboundCompletionRollsBackConstructionButNotResidents)
{
    ProfileState profile = makeNewAlphaProfile(
        "travel-construction-rollback",
        publishedContentRegistry());
    const WorldClockState startingClock = profile.worldClock;
    profile.baseConstruction.activeProject =
        ActiveBaseConstructionProject{
            BaseConstructionProjectDefinitionId{
                "base_construction.dormitory.level_2"},
            4U,
            3U,
            startingClock.elapsedWorldMinutes - 330U,
            startingClock.elapsedWorldMinutes + 30U};

    const DeployReceipt receipt = deploy(profile);
    ASSERT_TRUE(receipt.succeeded) << receipt.message;
    EXPECT_EQ(profile.baseConstruction.dormitoryLevel, 2U);
    EXPECT_FALSE(profile.baseConstruction.activeProject.has_value());
    EXPECT_EQ(profile.basePopulation.bedCapacity, 14U);
    // Models a rescue fact that must survive a later Raid rollback.
    ++profile.basePopulation.ordinaryResidents;
    ++profile.basePopulation.professionResidents[baseProfessionIndex(
        BaseResidentProfession::General)];

    ASSERT_TRUE(rollbackPendingRaidToBase(
        profile,
        publishedContentRegistry()).succeeded);
    EXPECT_EQ(profile.worldClock, startingClock);
    EXPECT_EQ(profile.baseConstruction.dormitoryLevel, 1U);
    ASSERT_TRUE(profile.baseConstruction.activeProject.has_value());
    EXPECT_EQ(
        profile.baseConstruction.activeProject->completionWorldMinute,
        startingClock.elapsedWorldMinutes + 30U);
    EXPECT_EQ(profile.basePopulation.bedCapacity, 10U);
    EXPECT_EQ(profile.basePopulation.ordinaryResidents, 9U);
}

TEST(RaidLifecycleTest, ManufacturingDefersAcrossRaidAndSettlesOrRollsBackSafely)
{
    ProfileState profile = makeNewAlphaProfile(
        "raid-manufacturing-boundary",
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
        CommandContext{profile.revision, "raid-start-manufacturing"});
    ASSERT_TRUE(started.succeeded) << started.message;
    static_cast<void>(advanceWorldClock(profile.worldClock, 330U));
    const WorldClockState preRaidClock = profile.worldClock;

    ASSERT_TRUE(deploy(profile).succeeded);
    ASSERT_TRUE(profile.pendingRaid.has_value());
    EXPECT_TRUE(profile.baseManufacturing.activeOrder.has_value());
    EXPECT_FALSE(profile.baseManufacturing.activeOrder->outputReady);

    ProfileState rollback = profile;
    ASSERT_TRUE(rollbackPendingRaidToBase(
        rollback, publishedContentRegistry()).succeeded);
    EXPECT_EQ(rollback.worldClock, preRaidClock);
    ASSERT_TRUE(rollback.baseManufacturing.activeOrder.has_value());
    EXPECT_FALSE(rollback.baseManufacturing.activeOrder->outputReady);

    const RaidSettlementReceipt settled = settlePendingRaid(
        profile,
        publishedContentRegistry(),
        profile.pendingRaid->settlementId,
        RaidResultOutcome::Extracted);
    ASSERT_TRUE(settled.succeeded) << settled.message;
    EXPECT_FALSE(profile.baseManufacturing.activeOrder.has_value());
    const AssetRecord *output = profile.assets.find(*started.outputAssetId);
    ASSERT_NE(output, nullptr);
    EXPECT_TRUE(std::holds_alternative<StoredAssetLocation>(output->location));
}

TEST(RaidLifecycleTest, CommittedMapRescueIsNotOfferedAgain)
{
    ProfileState profile = makeNewAlphaProfile(
        "rescue-not-repeated", publishedContentRegistry());
    profile.committedRescues.insert(
        RescueDefinitionId{"rescue.ordinary.greyline_depot"});

    ASSERT_TRUE(deploy(profile).succeeded);
    ASSERT_TRUE(profile.pendingRaid.has_value());
    EXPECT_FALSE(profile.pendingRaid->rescue.has_value());
}

TEST(RaidLifecycleTest, SecuredRescueSurvivesFailedRaidSettlementExactlyOnce)
{
    ProfileState profile = makeNewAlphaProfile(
        "rescue-failed-raid", publishedContentRegistry());
    ASSERT_TRUE(deploy(profile).succeeded);
    ASSERT_TRUE(profile.pendingRaid.has_value());
    ASSERT_TRUE(profile.pendingRaid->rescue.has_value());
    const RaidRescueSnapshot rescue = *profile.pendingRaid->rescue;
    profile.pendingRaid->rescue->secured = true;
    const OrdinarySurvivorAdmissionReceipt admitted =
        executeOrdinarySurvivorAdmission(
            profile,
            publishedContentRegistry(),
            OrdinarySurvivorAdmissionCommand{
                rescue.definitionId,
                rescue.ordinaryResidentCount},
            CommandContext{profile.revision, "rescue-before-death"});
    ASSERT_TRUE(admitted.succeeded) << admitted.message;
    EXPECT_EQ(profile.basePopulation.ordinaryResidents, 9U);

    const RaidSettlementReceipt settlement = settlePendingRaid(
        profile,
        publishedContentRegistry(),
        "settlement-alpha-test",
        RaidResultOutcome::PlayerDead);

    ASSERT_TRUE(settlement.succeeded) << settlement.message;
    EXPECT_EQ(profile.basePopulation.ordinaryResidents, 9U);
    ASSERT_TRUE(profile.lastRaidResult.has_value());
    EXPECT_EQ(profile.lastRaidResult->rescuedOrdinaryResidents, 1U);
    EXPECT_TRUE(profile.committedRescues.contains(rescue.definitionId));
}

TEST(RaidLifecycleTest, AshworksSettlementReportsAndKeepsInjuredResident)
{
    ProfileState profile = makeNewAlphaProfile(
        "rescue-injured-settlement", publishedContentRegistry());
    ASSERT_TRUE(deploy(
        profile,
        77231U,
        MapDefinitionId{"map.raid.industrial"}).succeeded);
    ASSERT_TRUE(profile.pendingRaid.has_value());
    ASSERT_TRUE(profile.pendingRaid->rescue.has_value());
    const RaidRescueSnapshot rescue = *profile.pendingRaid->rescue;
    ASSERT_EQ(rescue.injuredResidentCount, 1U);
    profile.pendingRaid->rescue->secured = true;
    ASSERT_TRUE(executeOrdinarySurvivorAdmission(
        profile,
        publishedContentRegistry(),
        OrdinarySurvivorAdmissionCommand{
            rescue.definitionId,
            rescue.ordinaryResidentCount,
            rescue.injuredResidentCount},
        CommandContext{profile.revision, "secure-injured-resident"})
                    .succeeded);

    const RaidSettlementReceipt settlement = settlePendingRaid(
        profile,
        publishedContentRegistry(),
        profile.pendingRaid->settlementId,
        RaidResultOutcome::Extracted);

    ASSERT_TRUE(settlement.succeeded) << settlement.message;
    EXPECT_EQ(profile.basePopulation.injuredResidents, 1U);
    ASSERT_TRUE(profile.lastRaidResult.has_value());
    EXPECT_EQ(profile.lastRaidResult->rescuedOrdinaryResidents, 1U);
    EXPECT_EQ(profile.lastRaidResult->rescuedInjuredResidents, 1U);
}

TEST(RaidLifecycleTest, SettlementUsesNormalOrFailureTravelExactlyOnce)
{
    for (const auto [outcome, expectedTravel] :
         {std::pair{RaidResultOutcome::Extracted, 45U},
          std::pair{RaidResultOutcome::ActiveQuit, 45U},
          std::pair{RaidResultOutcome::PlayerDead, 90U}})
    {
        ProfileState profile = makeNewAlphaProfile(
            "travel-settlement-" + std::to_string(expectedTravel) + "-" +
                std::to_string(static_cast<int>(outcome)),
            publishedContentRegistry());
        const std::uint64_t startingMinute =
            profile.worldClock.elapsedWorldMinutes;
        ASSERT_TRUE(deploy(profile).succeeded);

        const RaidSettlementReceipt settled = settlePendingRaid(
            profile,
            publishedContentRegistry(),
            "settlement-alpha-test",
            outcome);

        ASSERT_TRUE(settled.succeeded) << settled.message;
        ASSERT_TRUE(profile.lastRaidResult.has_value());
        EXPECT_EQ(profile.lastRaidResult->travelMinutesApplied,
                  expectedTravel);
        EXPECT_EQ(profile.worldClock.elapsedWorldMinutes,
                  startingMinute + 45U + expectedTravel);
        const std::uint64_t fingerprint = profileStateFingerprint(profile);
        const RaidSettlementReceipt repeated = settlePendingRaid(
            profile,
            publishedContentRegistry(),
            "settlement-alpha-test",
            outcome);
        EXPECT_TRUE(repeated.succeeded);
        EXPECT_TRUE(repeated.alreadyCommitted);
        EXPECT_EQ(profileStateFingerprint(profile), fingerprint);
    }
}

TEST(RaidLifecycleTest, AbnormalExitMustRollbackWithoutMutation)
{
    ProfileState profile = makeNewAlphaProfile(
        "travel-abnormal-rollback", publishedContentRegistry());
    ASSERT_TRUE(deploy(profile).succeeded);
    const std::uint64_t fingerprint = profileStateFingerprint(profile);

    const RaidSettlementReceipt rejected = settlePendingRaid(
        profile,
        publishedContentRegistry(),
        "settlement-alpha-test",
        RaidResultOutcome::AbnormalQuit);

    EXPECT_FALSE(rejected.succeeded);
    EXPECT_EQ(rejected.error, RaidLifecycleError::InvalidCommand);
    EXPECT_EQ(profileStateFingerprint(profile), fingerprint);
    ASSERT_TRUE(rollbackPendingRaidToBase(
        profile, publishedContentRegistry()).succeeded);
    EXPECT_EQ(profile.worldClock, WorldClockState{});
}

TEST(RaidLifecycleTest, RollbackRestoresPriorityAcrossCycleBoundary)
{
    ProfileState profile = makeNewAlphaProfile(
        "priority-cycle-rollback", publishedContentRegistry());
    profile.worldClock.elapsedWorldMinutes = 7630U;
    static_cast<void>(synchronizeBaseDailySystemsThrough(
        profile, publishedContentRegistry()));
    const BasePriorityState startingPriority = profile.basePriority;

    ASSERT_TRUE(deploy(
        profile,
        77231,
        MapDefinitionId{"map.raid.riverside"}).succeeded);
    EXPECT_EQ(profile.basePriority.cycleIndex, 1U);
    EXPECT_NE(profile.basePriority, startingPriority);

    ASSERT_TRUE(rollbackPendingRaidToBase(
        profile, publishedContentRegistry()).succeeded);
    EXPECT_EQ(profile.worldClock.elapsedWorldMinutes, 7630U);
    EXPECT_EQ(profile.basePriority, startingPriority);
}

TEST(RaidLifecycleTest, TravelAcrossMidnightResolvesDailyNeedOnce)
{
    ProfileState profile = makeNewAlphaProfile(
        "travel-midnight", publishedContentRegistry());
    profile.worldClock.elapsedWorldMinutes = 23U * 60U + 30U;
    profile.basePopulation = BasePopulationState{12, 10};

    ASSERT_TRUE(deploy(profile).succeeded);

    EXPECT_EQ(profile.worldClock.elapsedWorldMinutes,
              kWorldMinutesPerDay + 15U);
    EXPECT_EQ(profile.baseResources.resolvedDemandCycleCount, 1U);
    EXPECT_EQ(profile.baseResources.pool,
              (BaseResourceBundle{28, 34, 35, 36}));
    ASSERT_TRUE(settlePendingRaid(
        profile,
        publishedContentRegistry(),
        "settlement-alpha-test",
        RaidResultOutcome::Extracted).succeeded);
    EXPECT_EQ(profile.baseResources.resolvedDemandCycleCount, 1U);
    EXPECT_EQ(profile.baseResources.pool,
              (BaseResourceBundle{28, 34, 35, 36}));
}

TEST(RaidLifecycleTest, EveryPublishedRaidMapCreatesItsOwnDeterministicSnapshot)
{
    for (const MapDefinition &map : publishedContentRegistry().maps())
    {
        ProfileState first = makeNewAlphaProfile(
            "multi-map-first", publishedContentRegistry());
        ProfileState second = first;
        second.profileId = "multi-map-second";
        second.baseCommunityEvent = {};
        static_cast<void>(synchronizeBaseCommunityEventThrough(
            second,
            publishedContentRegistry()));

        ASSERT_TRUE(deploy(first, 77231, map.id).succeeded) << map.id.value();
        ASSERT_TRUE(deploy(second, 77231, map.id).succeeded) << map.id.value();
        ASSERT_TRUE(first.pendingRaid.has_value());
        ASSERT_TRUE(second.pendingRaid.has_value());
        EXPECT_EQ(first.pendingRaid->mapDefinitionId, map.id);
        EXPECT_EQ(first.pendingRaid->spawnExtractionPairId,
                  second.pendingRaid->spawnExtractionPairId);
        EXPECT_EQ(first.pendingRaid->enemyDeploymentId,
                  second.pendingRaid->enemyDeploymentId);
        EXPECT_EQ(first.pendingRaid->spatialLayout,
                  second.pendingRaid->spatialLayout);
        EXPECT_NE(first.pendingRaid->spatialLayout.layoutHash, 0U);
        ASSERT_EQ(first.pendingRaid->loot.size(), second.pendingRaid->loot.size());
        for (std::size_t index{}; index < first.pendingRaid->loot.size(); ++index)
        {
            EXPECT_EQ(first.pendingRaid->loot[index].assetId,
                      second.pendingRaid->loot[index].assetId);
            EXPECT_EQ(first.pendingRaid->loot[index].slotIndex,
                      second.pendingRaid->loot[index].slotIndex);
            EXPECT_FLOAT_EQ(first.pendingRaid->loot[index].position.x,
                            second.pendingRaid->loot[index].position.x);
            EXPECT_FLOAT_EQ(first.pendingRaid->loot[index].position.y,
                            second.pendingRaid->loot[index].position.y);
        }
    }
}

TEST(RaidLifecycleTest, ProceduralMapFreezesGeneratedCoverIntoSnapshot)
{
    ProfileState first = makeNewAlphaProfile(
        "procedural-map-first", publishedContentRegistry());
    ProfileState second = makeNewAlphaProfile(
        "procedural-map-second", publishedContentRegistry());
    const MapDefinitionId mapId{"map.raid.frontier_exchange"};

    ASSERT_TRUE(deploy(first, 817233U, mapId).succeeded);
    ASSERT_TRUE(deploy(second, 991827U, mapId).succeeded);
    ASSERT_TRUE(first.pendingRaid.has_value());
    ASSERT_TRUE(second.pendingRaid.has_value());
    EXPECT_FALSE(first.pendingRaid->spatialLayout.usedFallback);
    EXPECT_FALSE(second.pendingRaid->spatialLayout.usedFallback);
    EXPECT_NE(first.pendingRaid->spatialLayout.layoutHash,
              second.pendingRaid->spatialLayout.layoutHash);
    EXPECT_NE(first.pendingRaid->spatialLayout.ballisticBlockers,
              second.pendingRaid->spatialLayout.ballisticBlockers);
    EXPECT_TRUE(validateProfileState(
        first, publishedContentRegistry()).valid);
}

TEST(RaidLifecycleTest, UnknownRaidMapRejectsWithoutChangingProfile)
{
    ProfileState profile = makeNewAlphaProfile(
        "unknown-map", publishedContentRegistry());
    const std::uint64_t before = profileStateFingerprint(profile);

    const DeployReceipt receipt = deploy(
        profile, 77232, MapDefinitionId{"map.raid.unknown"});

    EXPECT_FALSE(receipt.succeeded);
    EXPECT_EQ(receipt.error, RaidLifecycleError::InvalidCommand);
    EXPECT_EQ(profileStateFingerprint(profile), before);
}

TEST(RaidLifecycleTest, OutboundClockOverflowRejectsWithoutChangingProfile)
{
    ProfileState profile = makeNewAlphaProfile(
        "travel-overflow", publishedContentRegistry());
    profile.worldClock.elapsedWorldMinutes =
        std::numeric_limits<std::uint64_t>::max() - 44U;
    profile.baseResources.resolvedDemandCycleCount =
        projectWorldClock(profile.worldClock).completedDays;
    const std::uint64_t fingerprint = profileStateFingerprint(profile);

    const DeployReceipt receipt = deploy(profile);

    EXPECT_FALSE(receipt.succeeded);
    EXPECT_EQ(receipt.error, RaidLifecycleError::RevisionOverflow);
    EXPECT_EQ(profileStateFingerprint(profile), fingerprint);
}
}

TEST(RaidLifecycleTest, DeployCreatesDeterministicFiniteSnapshot)
{
    ProfileState first = makeNewAlphaProfile(
        "raid-lifecycle-first",
        publishedContentRegistry());
    ProfileState second = first;
    second.profileId = "raid-lifecycle-second";
    equipAlphaLoadout(first);
    equipAlphaLoadout(second);

    ASSERT_TRUE(deploy(first).succeeded);
    ASSERT_TRUE(deploy(second).succeeded);
    ASSERT_TRUE(first.pendingRaid.has_value());
    ASSERT_TRUE(second.pendingRaid.has_value());
    EXPECT_EQ(first.pendingRaid->spawnExtractionPairId,
              second.pendingRaid->spawnExtractionPairId);
    EXPECT_EQ(first.pendingRaid->enemyDeploymentId,
              second.pendingRaid->enemyDeploymentId);
    EXPECT_EQ(first.pendingRaid->loot.size(), second.pendingRaid->loot.size());
    const auto regularLootCount = [](const PendingRaidSnapshot &raid)
    {
        return static_cast<std::size_t>(
            std::count_if(raid.loot.begin(),
                          raid.loot.end(),
                          [](const RaidLootSnapshot &loot)
                          { return !loot.requiresHighRisk; }));
    };
    EXPECT_GE(regularLootCount(*first.pendingRaid), 6U);
    EXPECT_LE(regularLootCount(*first.pendingRaid), 9U);
    EXPECT_EQ(first.pendingRaid->loot.size() -
                  regularLootCount(*first.pendingRaid),
              2U);
    EXPECT_GE(first.pendingRaid->enemies.size(), 4U);
    EXPECT_LE(first.pendingRaid->enemies.size(), 6U);
    EXPECT_TRUE(validateProfileState(first, publishedContentRegistry()).valid);
}

TEST(RaidLifecycleTest, DeploySnapshotTracksAllThreeWeaponRoots)
{
    ProfileState profile = makeNewAlphaProfile(
        "raid-lifecycle-multi-weapon",
        publishedContentRegistry());
    equipAlphaLoadout(profile);
    const ItemDefinition &rifleDefinition = publishedContentRegistry().item(
        alpha_content::rifle);
    const auto origin = findFirstProfileFit(
        profile,
        publishedContentRegistry(),
        ProfileContainerId::stash(),
        rifleDefinition,
        ItemOrientation::Degrees0,
        std::nullopt);
    ASSERT_TRUE(origin.has_value());
    const AssetInstanceId secondRifle = profile.assets.create(
        rifleDefinition,
        StoredAssetLocation{ProfileContainerId::stash(), *origin});
    ASSERT_TRUE(executeInventory(
        profile,
        publishedContentRegistry(),
        InventoryEquipCommand{
            secondRifle, EquipmentSlotKind::SecondaryWeapon},
        CommandContext{profile.revision, "equip-second-rifle"}).succeeded);
    const AssetInstanceId primary = *equippedAsset(
        profile, EquipmentSlotKind::PrimaryWeapon);
    const AssetInstanceId sidearm = *equippedAsset(
        profile, EquipmentSlotKind::Sidearm);

    ASSERT_TRUE(deploy(profile, 77125).succeeded);
    ASSERT_TRUE(profile.pendingRaid.has_value());
    const auto &roots = profile.pendingRaid->carriedRootAssetIds;
    EXPECT_NE(std::find(roots.begin(), roots.end(), primary), roots.end());
    EXPECT_NE(std::find(roots.begin(), roots.end(), secondRifle), roots.end());
    EXPECT_NE(std::find(roots.begin(), roots.end(), sidearm), roots.end());

    ASSERT_TRUE(rollbackPendingRaidToBase(
        profile, publishedContentRegistry()).succeeded);
    EXPECT_EQ(
        equippedAsset(profile, EquipmentSlotKind::PrimaryWeapon), primary);
    EXPECT_EQ(
        equippedAsset(profile, EquipmentSlotKind::SecondaryWeapon),
        secondRifle);
    EXPECT_EQ(equippedAsset(profile, EquipmentSlotKind::Sidearm), sidearm);
}

TEST(RaidLifecycleTest, DeployedRootMayMoveWithinCarriedOwnershipTree)
{
    ProfileState profile = makeNewAlphaProfile(
        "raid-lifecycle-carried-move",
        publishedContentRegistry());
    equipAlphaLoadout(profile);
    const AssetInstanceId rifle = *equippedAsset(
        profile, EquipmentSlotKind::PrimaryWeapon);
    const AssetInstanceId backpack = *equippedAsset(
        profile, EquipmentSlotKind::Backpack);
    ASSERT_TRUE(deploy(profile, 77126).succeeded);

    const ItemDefinition &rifleDefinition =
        publishedContentRegistry().item(alpha_content::rifle);
    const ProfileContainerId backpackGrid =
        ProfileContainerId::compartment(backpack, 0);
    const auto fit = findFirstProfileFit(
        profile,
        publishedContentRegistry(),
        backpackGrid,
        rifleDefinition,
        ItemOrientation::Degrees0,
        rifle);
    ASSERT_TRUE(fit.has_value());

    const InventoryReceipt moved = executeInventory(
        profile,
        publishedContentRegistry(),
        InventoryMoveCommand{
            rifle,
            0,
            StoredAssetLocation{backpackGrid, *fit},
            ItemOrientation::Degrees0},
        CommandContext{profile.revision, "raid-store-equipped-rifle"});
    ASSERT_TRUE(moved.succeeded) << moved.message;
    EXPECT_TRUE(assetIsCarried(profile, rifle));
    EXPECT_TRUE(validateProfileState(
        profile, publishedContentRegistry()).valid);

    const InventoryReceipt reequipped = executeInventory(
        profile,
        publishedContentRegistry(),
        InventoryEquipCommand{
            rifle, EquipmentSlotKind::SecondaryWeapon},
        CommandContext{profile.revision, "raid-reequip-rifle"});
    ASSERT_TRUE(reequipped.succeeded) << reequipped.message;
    EXPECT_EQ(
        equippedAsset(profile, EquipmentSlotKind::SecondaryWeapon),
        rifle);
}

TEST(RaidLifecycleTest, ExtractionRetainsCarriedAndPickedAssetsExactlyOnce)
{
    ProfileState profile = makeNewAlphaProfile(
        "raid-lifecycle-success",
        publishedContentRegistry());
    equipAlphaLoadout(profile);
    ASSERT_TRUE(deploy(profile).succeeded);
    ASSERT_TRUE(profile.pendingRaid.has_value());
    const AssetInstanceId loot = profile.pendingRaid->loot.front().assetId;
    ASSERT_TRUE(pickupRaidLoot(
        profile,
        publishedContentRegistry(),
        loot,
        CommandContext{profile.revision, "pickup-first-loot"}).succeeded);
    ASSERT_TRUE(assetIsCarried(profile, loot));
    const AssetLocation locationBeforeSettlement =
        profile.assets.find(loot)->location;
    std::map<AssetInstanceId, AssetLocation> carriedLocations;
    for (const auto &[id, asset] : profile.assets.records())
    {
        if (assetIsCarried(profile, id))
        {
            carriedLocations.emplace(id, asset.location);
        }
    }
    profile.medicalStatus = MedicalStatusState{
        BleedingSeverity::Heavy, 0, 400, 0, 18000};

    const RaidSettlementReceipt settled = settlePendingRaid(
        profile,
        publishedContentRegistry(),
        "settlement-alpha-test",
        RaidResultOutcome::Extracted);
    ASSERT_TRUE(settled.succeeded) << settled.message;
    EXPECT_FALSE(profile.pendingRaid.has_value());
    EXPECT_NE(profile.assets.find(loot), nullptr);
    EXPECT_EQ(
        profile.assets.find(loot)->location,
        locationBeforeSettlement);
    EXPECT_TRUE(assetIsCarried(profile, loot));
    for (const auto &[id, expectedLocation] : carriedLocations)
    {
        ASSERT_NE(profile.assets.find(id), nullptr);
        EXPECT_EQ(profile.assets.find(id)->location, expectedLocation);
    }
    EXPECT_TRUE(assetsInContainer(
        profile, ProfileContainerId::baseIntake()).empty());
    EXPECT_EQ(
        profile.baseResources.pool,
        (BaseResourceBundle{40, 40, 40, 40}));
    EXPECT_EQ(profile.baseResources.resolvedDemandCycleCount, 0U);
    EXPECT_EQ(profile.medicalStatus.bleeding, BleedingSeverity::Heavy);
    const std::uint64_t fingerprint = profileStateFingerprint(profile);

    const RaidSettlementReceipt repeated = settlePendingRaid(
        profile,
        publishedContentRegistry(),
        "settlement-alpha-test",
        RaidResultOutcome::Extracted);
    EXPECT_TRUE(repeated.succeeded);
    EXPECT_TRUE(repeated.alreadyCommitted);
    EXPECT_EQ(profileStateFingerprint(profile), fingerprint);
}

TEST(RaidLifecycleTest, PickupMayMergeAwayHistoricalSnapshotAsset)
{
    ProfileState profile = makeNewAlphaProfile(
        "raid-lifecycle-merge-loot",
        publishedContentRegistry());
    equipAlphaLoadout(profile);
    const AssetInstanceId backpack = *equippedAsset(
        profile,
        EquipmentSlotKind::Backpack);
    const AssetInstanceId ammunition =
        firstAsset(profile, alpha_content::ammunition);
    profile.assets.findMutable(ammunition)->quantity = 55;
    ASSERT_TRUE(executeInventory(
        profile,
        publishedContentRegistry(),
        InventoryMoveCommand{
            ammunition,
            0,
            StoredAssetLocation{
                ProfileContainerId::compartment(backpack, 0),
                GridPosition{0, 0}},
            ItemOrientation::Degrees0},
        CommandContext{profile.revision, "store-ammunition"}).succeeded);
    ASSERT_TRUE(deploy(profile, 8821).succeeded);

    const AssetInstanceId loot = profile.pendingRaid->loot.front().assetId;
    AssetRecord *lootAsset = profile.assets.findMutable(loot);
    ASSERT_NE(lootAsset, nullptr);
    lootAsset->definitionId = alpha_content::ammunition;
    lootAsset->quantity = 5;
    lootAsset->reliefBatchId.reset();
    profile.pendingRaid->loot.front().definitionId =
        alpha_content::ammunition;
    profile.pendingRaid->loot.front().quantity = 5;
    ASSERT_TRUE(validateProfileState(
        profile,
        publishedContentRegistry()).valid);

    const InventoryReceipt pickedUp = pickupRaidLoot(
        profile,
        publishedContentRegistry(),
        loot,
        CommandContext{profile.revision, "merge-picked-ammunition"});

    ASSERT_TRUE(pickedUp.succeeded) << pickedUp.message;
    EXPECT_EQ(profile.assets.find(ammunition)->quantity, 60U);
    EXPECT_EQ(profile.assets.find(loot), nullptr);
    EXPECT_TRUE(validateProfileState(
        profile,
        publishedContentRegistry()).valid);

    const RaidSettlementReceipt settled = settlePendingRaid(
        profile,
        publishedContentRegistry(),
        "settlement-alpha-test",
        RaidResultOutcome::Extracted);
    ASSERT_TRUE(settled.succeeded) << settled.message;
    EXPECT_EQ(profile.assets.find(ammunition)->quantity, 60U);
    EXPECT_EQ(
        std::get<StoredAssetLocation>(
            profile.assets.find(ammunition)->location).container,
        ProfileContainerId::compartment(backpack, 0));
    EXPECT_TRUE(assetsInContainer(
        profile, ProfileContainerId::baseIntake()).empty());
}

TEST(RaidLifecycleTest, DeathTransfersCarriedAssetsToLostRecordAndResetsHealth)
{
    ProfileState profile = makeNewAlphaProfile(
        "raid-lifecycle-failure",
        publishedContentRegistry());
    equipAlphaLoadout(profile);
    profile.currentHealth = 25;
    profile.medicalStatus = MedicalStatusState{
        BleedingSeverity::Light, 22000, 700, 0, 16000};
    ASSERT_TRUE(deploy(profile, 4471).succeeded);

    const RaidSettlementReceipt settled = settlePendingRaid(
        profile,
        publishedContentRegistry(),
        "settlement-alpha-test",
        RaidResultOutcome::PlayerDead);
    ASSERT_TRUE(settled.succeeded) << settled.message;
    EXPECT_FALSE(profile.pendingRaid.has_value());
    EXPECT_EQ(profile.currentHealth, 100);
    EXPECT_EQ(profile.medicalStatus, MedicalStatusState{});
    EXPECT_FALSE(equippedAsset(
        profile,
        EquipmentSlotKind::PrimaryWeapon).has_value());
    EXPECT_FALSE(equippedAsset(
        profile,
        EquipmentSlotKind::Sidearm).has_value());
    EXPECT_FALSE(equippedAsset(
        profile,
        EquipmentSlotKind::ChestRig).has_value());
    EXPECT_FALSE(equippedAsset(
        profile,
        EquipmentSlotKind::Backpack).has_value());
    EXPECT_FALSE(equippedAsset(
        profile,
        EquipmentSlotKind::Helmet).has_value());
    EXPECT_FALSE(equippedAsset(
        profile,
        EquipmentSlotKind::BodyArmor).has_value());
    ASSERT_EQ(profile.lostRaidRecords.size(), 1U);
    ASSERT_TRUE(profile.lastRaidResult.has_value());
    ASSERT_TRUE(profile.lastRaidResult->lostRaidRecordId.has_value());
    EXPECT_EQ(
        *profile.lastRaidResult->lostRaidRecordId,
        "settlement-alpha-test");
    for (const auto &[id, asset] : profile.assets.records())
    {
        static_cast<void>(id);
        EXPECT_FALSE(std::holds_alternative<RaidGroundAssetLocation>(asset.location));
    }
}

TEST(RaidLifecycleTest, LegacyPendingRaidRollbackKeepsEntryLoadout)
{
    ProfileState profile = makeNewAlphaProfile(
        "raid-lifecycle-rollback",
        publishedContentRegistry());
    equipAlphaLoadout(profile);
    const auto rifle = equippedAsset(
        profile, EquipmentSlotKind::PrimaryWeapon);
    ASSERT_TRUE(rifle.has_value());
    profile.currentHealth = 70;
    profile.medicalStatus = MedicalStatusState{
        BleedingSeverity::Light, 31000, 900, 0, 17000};
    const MedicalStatusState entryMedical = profile.medicalStatus;
    ASSERT_TRUE(deploy(profile, 4481).succeeded);
    ASSERT_TRUE(profile.pendingRaid.has_value());
    const std::vector<RaidLootSnapshot> generatedLoot =
        profile.pendingRaid->loot;

    const RaidRollbackReceipt rolledBack = rollbackPendingRaidToBase(
        profile,
        publishedContentRegistry());

    ASSERT_TRUE(rolledBack.succeeded) << rolledBack.message;
    EXPECT_FALSE(profile.pendingRaid.has_value());
    EXPECT_EQ(profile.currentHealth, 70);
    EXPECT_EQ(profile.medicalStatus, entryMedical);
    EXPECT_EQ(equippedAsset(
        profile, EquipmentSlotKind::PrimaryWeapon), rifle);
    EXPECT_FALSE(profile.lastRaidResult.has_value());
    for (const RaidLootSnapshot &loot : generatedLoot)
    {
        EXPECT_EQ(profile.assets.find(loot.assetId), nullptr);
    }
    EXPECT_TRUE(validateProfileState(
        profile, publishedContentRegistry()).valid);
}

TEST(RaidLifecycleTest, LegacyUnassignedReturnsDoNotBlockDeploy)
{
    ProfileState profile = makeNewAlphaProfile(
        "raid-lifecycle-allocation-gate",
        publishedContentRegistry());
    const ItemDefinition &definition = publishedContentRegistry().item(
        alpha_content::lootCola);
    static_cast<void>(profile.assets.create(
        definition,
        StoredAssetLocation{
            ProfileContainerId::baseIntake(), GridPosition{0, 0}},
        1));
    const DeployReceipt receipt = deploy(profile, 9901);

    ASSERT_TRUE(receipt.succeeded) << receipt.message;
    EXPECT_TRUE(profile.pendingRaid.has_value());
    EXPECT_EQ(
        assetsInContainer(
            profile, ProfileContainerId::baseIntake()).size(),
        1U);
}
