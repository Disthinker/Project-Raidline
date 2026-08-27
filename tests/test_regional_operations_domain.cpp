#include <gtest/gtest.h>

#include "regional_operations_domain.h"

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
