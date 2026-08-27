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
