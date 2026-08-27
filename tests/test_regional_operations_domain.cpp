#include <gtest/gtest.h>

#include "base_workforce_domain.h"
#include "regional_operations_domain.h"

TEST(RegionalOperationsDomainTest,
     BaseSiteClearanceQueryIsPureAndUsesPublishedMissionMap)
{
    const ContentRegistry &content = publishedContentRegistry();
    ProfileState profile = makeNewAlphaProfile(
        "regional-base-site-clearance-query", content);
    const RegionalBaseSiteDefinitionId siteId{
        "regional_base_site.ashworks_logistics_yard"};
    const RegionalOutpostDefinitionId outpostId{
        "regional_outpost.ashworks_logistics_yard"};
    ASSERT_TRUE(profile.regionalOperations.baseSites.at(
        RegionalBaseSiteDefinitionId{"regional_base_site.greyline_yard"})
                    .unlocked);
    ASSERT_FALSE(profile.regionalOperations.baseSites.at(siteId).unlocked);
    ASSERT_FALSE(profile.regionalOperations.outposts.at(outpostId).unlocked);
    const std::uint64_t fingerprint = profileStateFingerprint(profile);

    const RegionalBaseSiteClearancePlan plan =
        queryRegionalBaseSiteClearance(profile, content, siteId);

    ASSERT_TRUE(plan.canDeploy) << plan.message;
    EXPECT_EQ(plan.mapDefinitionId, MapDefinitionId{"map.raid.industrial"});
    EXPECT_FALSE(plan.unlocked);
    EXPECT_EQ(profileStateFingerprint(profile), fingerprint);

    profile.pendingRaid = PendingRaidSnapshot{};
    const std::uint64_t pendingFingerprint =
        profileStateFingerprint(profile);
    const RegionalBaseSiteClearancePlan blocked =
        queryRegionalBaseSiteClearance(profile, content, siteId);
    EXPECT_FALSE(blocked.canDeploy);
    EXPECT_EQ(blocked.error, DomainErrorCode::IllegalDestination);
    EXPECT_EQ(profileStateFingerprint(profile), pendingFingerprint);
}

TEST(RegionalOperationsDomainTest,
     NewProfileUsesStableDirectRouteWithoutMutation)
{
    const ContentRegistry &content = publishedContentRegistry();
    const ProfileState profile = makeNewAlphaProfile(
        "regional-direct-route", content);
    const std::uint64_t fingerprint = profileStateFingerprint(profile);

    const RegionalRoutePlan route = queryRegionalRoute(
        profile, content, MapDefinitionId{"map.raid.industrial"});

    ASSERT_TRUE(route.reachable) << route.message;
    EXPECT_EQ(route.travelMinutes, 150U);
    ASSERT_EQ(route.routeIds.size(), 1U);
    EXPECT_EQ(
        route.routeIds.front(),
        RegionRouteDefinitionId{"region_route.industrial_direct"});
    EXPECT_FALSE(route.usesOnlineOutpost);
    EXPECT_EQ(profileStateFingerprint(profile), fingerprint);
}

TEST(RegionalOperationsDomainTest,
     UnknownMapIsRejectedWithoutMutation)
{
    const ContentRegistry &content = publishedContentRegistry();
    const ProfileState profile = makeNewAlphaProfile(
        "regional-unknown-route", content);
    const std::uint64_t fingerprint = profileStateFingerprint(profile);

    const RegionalRoutePlan route = queryRegionalRoute(
        profile, content, MapDefinitionId{"map.unknown"});

    EXPECT_FALSE(route.reachable);
    EXPECT_EQ(route.error, DomainErrorCode::InvalidProfile);
    EXPECT_EQ(profileStateFingerprint(profile), fingerprint);
}

TEST(RegionalOperationsDomainTest,
     OutpostRoutesRemainClosedUntilFullyOperational)
{
    const ContentRegistry &content = publishedContentRegistry();
    ProfileState profile = makeNewAlphaProfile(
        "regional-closed-shortcut", content);
    const RegionalOutpostDefinitionId outpostId{
        "regional_outpost.old_service_relay"};
    RegionalOutpostState &outpost =
        profile.regionalOperations.outposts.at(outpostId);
    outpost.established = true;
    outpost.assignedStaff[0] = 1U;

    const RegionalRoutePlan understaffed = queryRegionalRoute(
        profile, content, MapDefinitionId{"map.raid.riverside"});

    ASSERT_TRUE(understaffed.reachable);
    EXPECT_EQ(understaffed.travelMinutes, 90U);
    EXPECT_FALSE(understaffed.usesOnlineOutpost);

    outpost.assignedStaff[0] = 2U;
    const RegionalRoutePlan online = queryRegionalRoute(
        profile, content, MapDefinitionId{"map.raid.riverside"});

    ASSERT_TRUE(online.reachable);
    EXPECT_EQ(online.travelMinutes, 55U);
    ASSERT_EQ(online.routeIds.size(), 2U);
    EXPECT_TRUE(online.usesOnlineOutpost);
}

TEST(RegionalOperationsDomainTest,
     SettledShortcutAdvancesOneThreatStepAcrossSeveralRouteEdges)
{
    const ContentRegistry &content = publishedContentRegistry();
    ProfileState profile = makeNewAlphaProfile(
        "outpost-threat-route", content);
    const RegionalOutpostDefinitionId outpostId{
        "regional_outpost.old_service_relay"};
    RegionalOutpostState &outpost =
        profile.regionalOperations.outposts.at(outpostId);
    outpost.established = true;
    outpost.assignedStaff[static_cast<std::size_t>(
        BaseResidentProfession::General)] = 2U;
    const std::uint64_t before = profileStateFingerprint(profile);

    const RegionalOutpostThreatAdvance advanced =
        applySettledRegionalRouteUsage(
            profile,
            content,
            {RegionRouteDefinitionId{"region_route.relay_access"},
             RegionRouteDefinitionId{
                 "region_route.relay_industrial_shortcut"}});

    ASSERT_TRUE(advanced.succeeded) << advanced.message;
    ASSERT_EQ(advanced.usedOutpostIds.size(), 1U);
    EXPECT_TRUE(advanced.newlyDisruptedOutpostIds.empty());
    EXPECT_EQ(outpost.shortcutOperationsSinceRestoration, 1U);
    EXPECT_FALSE(outpost.disrupted);
    EXPECT_NE(profileStateFingerprint(profile), before);
}

TEST(RegionalOperationsDomainTest,
     ThreatThresholdDisruptsOutpostAndInvalidRouteLeavesStateUnchanged)
{
    const ContentRegistry &content = publishedContentRegistry();
    ProfileState profile = makeNewAlphaProfile(
        "outpost-threat-threshold", content);
    const RegionalOutpostDefinitionId outpostId{
        "regional_outpost.old_service_relay"};
    RegionalOutpostState &outpost =
        profile.regionalOperations.outposts.at(outpostId);
    outpost.established = true;
    outpost.assignedStaff[static_cast<std::size_t>(
        BaseResidentProfession::General)] = 2U;
    outpost.shortcutOperationsSinceRestoration = 2U;

    const RegionalOutpostThreatAdvance disrupted =
        applySettledRegionalRouteUsage(
            profile,
            content,
            {RegionRouteDefinitionId{"region_route.relay_access"}});
    ASSERT_TRUE(disrupted.succeeded) << disrupted.message;
    ASSERT_EQ(disrupted.newlyDisruptedOutpostIds.size(), 1U);
    EXPECT_EQ(disrupted.newlyDisruptedOutpostIds.front(), outpostId);
    EXPECT_EQ(outpost.shortcutOperationsSinceRestoration, 3U);
    EXPECT_TRUE(outpost.disrupted);
    EXPECT_FALSE(regionalOutpostOnline(profile, content, outpostId));

    const std::uint64_t fingerprint = profileStateFingerprint(profile);
    const RegionalOutpostThreatAdvance rejected =
        applySettledRegionalRouteUsage(
            profile,
            content,
            {RegionRouteDefinitionId{"region_route.relay_access"}});
    EXPECT_FALSE(rejected.succeeded);
    EXPECT_EQ(profileStateFingerprint(profile), fingerprint);
}

TEST(RegionalOperationsDomainTest,
     EstablishOutpostIsAtomicIdempotentAndStillOffline)
{
    const ContentRegistry &content = publishedContentRegistry();
    ProfileState profile = makeNewAlphaProfile(
        "regional-establish", content);
    const RegionalOutpostDefinitionId outpostId{
        "regional_outpost.old_service_relay"};

    const RegionalOutpostReceipt receipt =
        executeEstablishRegionalOutpost(
            profile,
            content,
            EstablishRegionalOutpostCommand{outpostId},
            CommandContext{profile.revision, "establish-relay"});

    ASSERT_TRUE(receipt.succeeded) << receipt.message;
    EXPECT_TRUE(profile.regionalOperations.outposts.at(outpostId)
                    .established);
    EXPECT_FALSE(regionalOutpostOnline(profile, content, outpostId));
    const ProfileRevision committedRevision = profile.revision;
    const std::uint64_t committedFingerprint =
        profileStateFingerprint(profile);

    const RegionalOutpostReceipt replay =
        executeEstablishRegionalOutpost(
            profile,
            content,
            EstablishRegionalOutpostCommand{outpostId},
            CommandContext{0U, "establish-relay"});
    EXPECT_TRUE(replay.succeeded);
    EXPECT_TRUE(replay.alreadyCommitted);
    EXPECT_EQ(profile.revision, committedRevision);
    EXPECT_EQ(profileStateFingerprint(profile), committedFingerprint);
}

TEST(RegionalOperationsDomainTest,
     LockedOrRaidPendingEstablishmentRejectsWithoutMutation)
{
    const ContentRegistry &content = publishedContentRegistry();
    const RegionalOutpostDefinitionId outpostId{
        "regional_outpost.old_service_relay"};
    ProfileState locked = makeNewAlphaProfile(
        "regional-establish-locked", content);
    locked.regionalOperations.outposts.at(outpostId).unlocked = false;
    const std::uint64_t lockedFingerprint =
        profileStateFingerprint(locked);

    const RegionalOutpostReceipt lockedReceipt =
        executeEstablishRegionalOutpost(
            locked,
            content,
            EstablishRegionalOutpostCommand{outpostId},
            CommandContext{locked.revision, "establish-locked"});
    EXPECT_FALSE(lockedReceipt.succeeded);
    EXPECT_EQ(profileStateFingerprint(locked), lockedFingerprint);

    ProfileState pending = makeNewAlphaProfile(
        "regional-establish-pending", content);
    pending.pendingRaid = PendingRaidSnapshot{};
    const std::uint64_t pendingFingerprint =
        profileStateFingerprint(pending);
    const RegionalOutpostReceipt pendingReceipt =
        executeEstablishRegionalOutpost(
            pending,
            content,
            EstablishRegionalOutpostCommand{outpostId},
            CommandContext{pending.revision, "establish-pending"});
    EXPECT_FALSE(pendingReceipt.succeeded);
    EXPECT_EQ(profileStateFingerprint(pending), pendingFingerprint);
}

TEST(RegionalOperationsDomainTest,
     FullGarrisonUsesUnassignedHealthyResidentsAndActivatesShortcuts)
{
    const ContentRegistry &content = publishedContentRegistry();
    ProfileState profile = makeNewAlphaProfile(
        "regional-staffing", content);
    const RegionalOutpostDefinitionId outpostId{
        "regional_outpost.old_service_relay"};
    ASSERT_TRUE(executeEstablishRegionalOutpost(
        profile,
        content,
        EstablishRegionalOutpostCommand{outpostId},
        CommandContext{profile.revision, "staffing-establish"})
                    .succeeded);
    const BaseWorkforceProjection before = projectBaseWorkforce(profile);

    const RegionalOutpostStaffingReceipt assigned =
        executeRegionalOutpostStaffing(
            profile,
            content,
            RegionalOutpostStaffingCommand{outpostId, true},
            CommandContext{profile.revision, "staffing-assign"});

    ASSERT_TRUE(assigned.succeeded) << assigned.message;
    EXPECT_EQ(assigned.assignedStaff, 2U);
    EXPECT_EQ(
        assigned.assignedByProfession[baseProfessionIndex(
            BaseResidentProfession::General)],
        2U);
    EXPECT_TRUE(assigned.online);
    const BaseWorkforceProjection after = projectBaseWorkforce(profile);
    EXPECT_EQ(after.availableResidents + 2U, before.availableResidents);
    EXPECT_EQ(after.assignedResidents, before.assignedResidents + 2U);

    const RegionalRoutePlan route = queryRegionalRoute(
        profile, content, MapDefinitionId{"map.raid.frontier_exchange"});
    ASSERT_TRUE(route.reachable);
    EXPECT_EQ(route.travelMinutes, 125U);
    EXPECT_TRUE(route.usesOnlineOutpost);

    const RegionalOutpostStaffingReceipt cleared =
        executeRegionalOutpostStaffing(
            profile,
            content,
            RegionalOutpostStaffingCommand{outpostId, false},
            CommandContext{profile.revision, "staffing-clear"});
    ASSERT_TRUE(cleared.succeeded) << cleared.message;
    EXPECT_EQ(cleared.assignedStaff, 0U);
    EXPECT_FALSE(cleared.online);
    EXPECT_EQ(
        queryRegionalRoute(
            profile, content,
            MapDefinitionId{"map.raid.frontier_exchange"})
            .travelMinutes,
        210U);
}

TEST(RegionalOperationsDomainTest,
     ConstructionReservationCanBlockGarrisonWithoutMutation)
{
    const ContentRegistry &content = publishedContentRegistry();
    ProfileState profile = makeNewAlphaProfile(
        "regional-staffing-reserved", content);
    const RegionalOutpostDefinitionId outpostId{
        "regional_outpost.old_service_relay"};
    profile.basePopulation.ordinaryResidents = 4U;
    profile.basePopulation.professionResidents = {2U, 1U, 1U, 0U};
    profile.baseWorkforce = {};
    profile.baseWorkforce.workshopWorker.reset();
    profile.baseWorkforce.medicalWorker.reset();
    profile.baseConstruction.activeProject = ActiveBaseConstructionProject{
        BaseConstructionProjectDefinitionId{
            "base_construction.dormitory.level_2"},
        4U,
        3U,
        profile.worldClock.elapsedWorldMinutes,
        profile.worldClock.elapsedWorldMinutes + 360U};
    profile.regionalOperations.outposts.at(outpostId).established = true;
    ASSERT_TRUE(validateProfileState(profile, content).valid);
    const std::uint64_t fingerprint = profileStateFingerprint(profile);

    const RegionalOutpostStaffingReceipt receipt =
        executeRegionalOutpostStaffing(
            profile,
            content,
            RegionalOutpostStaffingCommand{outpostId, true},
            CommandContext{profile.revision, "staffing-reserved"});

    EXPECT_FALSE(receipt.succeeded);
    EXPECT_EQ(receipt.error, DomainErrorCode::Capacity);
    EXPECT_EQ(profileStateFingerprint(profile), fingerprint);
}
