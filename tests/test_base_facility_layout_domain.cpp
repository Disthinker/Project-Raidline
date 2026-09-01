#include <gtest/gtest.h>

#include <algorithm>
#include <utility>

#include "base_facility_layout_domain.h"

namespace
{
const RegionalBaseSiteDefinitionId kGreyline{
    "regional_base_site.greyline_yard"};
const BaseFacilityDefinitionId kWarehouse{"base_facility.warehouse"};

BaseFacilityLayoutAccess access(std::vector<ContentRect> blockers = {})
{
    return BaseFacilityLayoutAccess{
        kGreyline,
        ContentRect{{1000.0F, 2000.0F}, {1600.0F, 1120.0F}},
        std::move(blockers)};
}
}

TEST(BaseFacilityLayoutDomainTest,
     RepositionIsPureThenCommitsNormalizedStablePosition)
{
    const ContentRegistry &content = publishedContentRegistry();
    ProfileState profile = makeNewPublishedProfile("facility-layout", content);
    const RepositionBaseFacilityCommand command{
        kWarehouse, {1400.0F, 2400.0F}, {300.0F, 220.0F}, access()};

    const std::uint64_t before = profileStateFingerprint(profile);
    const BaseFacilityLayoutPlan plan = queryBaseFacilityLayout(
        profile, content, command);
    ASSERT_TRUE(plan.canCommit) << plan.message;
    EXPECT_EQ(profileStateFingerprint(profile), before);
    EXPECT_FLOAT_EQ(plan.normalizedCenter.x, 0.25F);
    EXPECT_FLOAT_EQ(plan.normalizedCenter.y, 400.0F / 1120.0F);

    const BaseFacilityLayoutReceipt receipt = executeBaseFacilityLayout(
        profile, content, command,
        CommandContext{profile.revision, "move-warehouse"});
    ASSERT_TRUE(receipt.succeeded) << receipt.message;
    EXPECT_FALSE(receipt.alreadyCommitted);
    const auto site = profile.baseFacilityLayout.placements.find(kGreyline);
    ASSERT_NE(site, profile.baseFacilityLayout.placements.end());
    EXPECT_FLOAT_EQ(
        site->second.at(kWarehouse).x, plan.normalizedCenter.x);
    EXPECT_FLOAT_EQ(
        site->second.at(kWarehouse).y, plan.normalizedCenter.y);

    const auto projection = projectBaseFacilityLayout(
        profile, kGreyline, access().baseParcel);
    const auto projected = std::find_if(
        projection.begin(), projection.end(),
        [](const BaseFacilitySpatialProjection &candidate)
        { return candidate.facilityDefinitionId == kWarehouse; });
    ASSERT_NE(projected, projection.end());
    EXPECT_FLOAT_EQ(projected->worldCenter.x, 1400.0F);
    EXPECT_FLOAT_EQ(projected->worldCenter.y, 2400.0F);
}

TEST(BaseFacilityLayoutDomainTest,
     BlockedReserveAndStaleMovesRejectWithoutMutation)
{
    const ContentRegistry &content = publishedContentRegistry();
    ProfileState profile = makeNewPublishedProfile("facility-reject", content);
    const RepositionBaseFacilityCommand blocked{
        kWarehouse,
        {1400.0F, 2400.0F},
        {300.0F, 220.0F},
        access({ContentRect{{1300.0F, 2300.0F}, {200.0F, 200.0F}}})};
    const std::uint64_t before = profileStateFingerprint(profile);
    EXPECT_FALSE(queryBaseFacilityLayout(profile, content, blocked).canCommit);
    EXPECT_FALSE(executeBaseFacilityLayout(
        profile, content, blocked,
        CommandContext{profile.revision, "blocked"}).succeeded);
    EXPECT_EQ(profileStateFingerprint(profile), before);

    profile.baseConstruction.facilities[kWarehouse] =
        BaseConstructionState::FacilityPlacement::Reserve;
    profile.baseConstruction.facilityReserveStartedWorldMinutes[kWarehouse] =
        profile.worldClock.elapsedWorldMinutes;
    const RepositionBaseFacilityCommand legalGeometry{
        kWarehouse, {1400.0F, 2400.0F}, {300.0F, 220.0F}, access()};
    EXPECT_FALSE(queryBaseFacilityLayout(
        profile, content, legalGeometry).canCommit);

    profile.baseConstruction.facilities[kWarehouse] =
        BaseConstructionState::FacilityPlacement::Installed;
    profile.baseConstruction.facilityReserveStartedWorldMinutes.erase(
        kWarehouse);
    const std::uint64_t restored = profileStateFingerprint(profile);
    EXPECT_FALSE(executeBaseFacilityLayout(
        profile, content, legalGeometry,
        CommandContext{profile.revision + 1U, "stale"}).succeeded);
    EXPECT_EQ(profileStateFingerprint(profile), restored);
}

TEST(BaseFacilityLayoutDomainTest,
     LegacyInitializationCoversEveryPublishedBaseSite)
{
    const ContentRegistry &content = publishedContentRegistry();
    ProfileState profile = makeNewPublishedProfile("facility-init", content);
    profile.baseFacilityLayout = {};
    initializeBaseFacilityLayouts(profile, content);
    ASSERT_EQ(
        profile.baseFacilityLayout.placements.size(),
        content.regionalOperations().baseSites.size());
    for (const auto &[site, placements] :
         profile.baseFacilityLayout.placements)
    {
        static_cast<void>(site);
        EXPECT_TRUE(placements.contains(kWarehouse));
        EXPECT_TRUE(placements.contains(
            BaseFacilityDefinitionId{"base_facility.medical"}));
        EXPECT_TRUE(placements.contains(
            BaseFacilityDefinitionId{"base_facility.dormitory"}));
        EXPECT_TRUE(placements.contains(
            BaseFacilityDefinitionId{"base_facility.workshop"}));
    }
}
