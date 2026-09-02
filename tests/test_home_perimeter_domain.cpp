#include <gtest/gtest.h>

#include "home_perimeter_domain.h"

namespace
{
const RegionalBaseSiteDefinitionId kGreyline{
    "regional_base_site.greyline_yard"};

HomePerimeterGenerationContext context()
{
    return HomePerimeterGenerationContext{
        kGreyline,
        {12800.0F, 7200.0F},
        {{5200.0F, 2800.0F}, {1600.0F, 1100.0F}},
        {{{2400.0F, 1200.0F}, {900.0F, 500.0F}},
         {{8200.0F, 4700.0F}, {700.0F, 900.0F}}}};
}
}

TEST(HomePerimeterDomainTest, SafetyZoneUsesCoreAndTransitionBuffer)
{
    const ContentRect core{{1000.0F, 1000.0F}, {1200.0F, 800.0F}};
    EXPECT_EQ(queryHomeRegionSafetyZone({1100.0F, 1100.0F}, core),
              HomeRegionSafetyZone::SafeCore);
    EXPECT_EQ(queryHomeRegionSafetyZone({800.0F, 1100.0F}, core),
              HomeRegionSafetyZone::TransitionBuffer);
    EXPECT_EQ(queryHomeRegionSafetyZone({100.0F, 100.0F}, core),
              HomeRegionSafetyZone::Perimeter);
}

TEST(HomePerimeterDomainTest, SnapshotIsDeterministicAndDoesNotReroll)
{
    const ContentRegistry &content = publishedContentRegistry();
    ProfileState first = makeNewPublishedProfile("perimeter-a", content);
    ProfileState second = makeNewPublishedProfile("perimeter-b", content);

    const HomePerimeterEnsureReceipt firstReceipt =
        ensureHomePerimeterSnapshot(
            first, content, context(),
            {first.revision, "perimeter-generate"});
    const HomePerimeterEnsureReceipt secondReceipt =
        ensureHomePerimeterSnapshot(
            second, content, context(),
            {second.revision, "perimeter-generate"});
    ASSERT_TRUE(firstReceipt.succeeded) << firstReceipt.message;
    ASSERT_TRUE(secondReceipt.succeeded) << secondReceipt.message;
    const auto &left = first.homePerimeter.sites.at(kGreyline);
    const auto &right = second.homePerimeter.sites.at(kGreyline);
    EXPECT_EQ(left.enemies, right.enemies);
    EXPECT_EQ(left.seed, right.seed);
    ASSERT_EQ(left.lootAssetIds.size(), right.lootAssetIds.size());
    for (std::size_t index = 0; index < left.lootAssetIds.size(); ++index)
    {
        EXPECT_EQ(first.assets.find(left.lootAssetIds[index])->definitionId,
                  second.assets.find(right.lootAssetIds[index])->definitionId);
    }

    const auto again = ensureHomePerimeterSnapshot(
        first, content, context(),
        {first.revision, "perimeter-generate-again"});
    EXPECT_TRUE(again.succeeded);
    EXPECT_FALSE(again.changed);
    EXPECT_EQ(first.homePerimeter.sites.at(kGreyline), left);
}

TEST(HomePerimeterDomainTest, ReturnAndRescueAreDistinctIdempotentResults)
{
    const ContentRegistry &content = publishedContentRegistry();
    ProfileState profile = makeNewPublishedProfile("perimeter-result", content);
    ASSERT_TRUE(ensureHomePerimeterSnapshot(
        profile, content, context(),
        {profile.revision, "generate"}).succeeded);
    ASSERT_TRUE(beginHomePerimeterOuting(
        profile, kGreyline,
        {profile.revision, "outing-1"}).succeeded);
    const std::uint64_t beforeReturn = profile.worldClock.elapsedWorldMinutes;
    const auto returned = completeHomePerimeterOuting(
        profile, false, {profile.revision, "return-1"});
    ASSERT_TRUE(returned.succeeded) << returned.message;
    EXPECT_EQ(profile.worldClock.elapsedWorldMinutes,
              beforeReturn + kHomePerimeterReturnMinutes);
    const std::uint64_t returnedFingerprint = profileStateFingerprint(profile);
    const auto repeated = completeHomePerimeterOuting(
        profile, false, {1U, "return-1"});
    EXPECT_TRUE(repeated.succeeded);
    EXPECT_TRUE(repeated.alreadyCommitted);
    EXPECT_EQ(profileStateFingerprint(profile), returnedFingerprint);

    ASSERT_TRUE(beginHomePerimeterOuting(
        profile, kGreyline,
        {profile.revision, "outing-2"}).succeeded);
    profile.currentHealth = 1;
    const std::uint64_t beforeRescue = profile.worldClock.elapsedWorldMinutes;
    const auto rescued = completeHomePerimeterOuting(
        profile, true, {profile.revision, "rescue-2"});
    ASSERT_TRUE(rescued.succeeded) << rescued.message;
    EXPECT_EQ(profile.currentHealth, kHomePerimeterRescueHealth);
    EXPECT_EQ(profile.worldClock.elapsedWorldMinutes,
              beforeRescue + kHomePerimeterRescueMinutes);
    EXPECT_FALSE(profile.pendingRaid.has_value());
    EXPECT_FALSE(profile.lastRaidResult.has_value());
    EXPECT_TRUE(profile.lostRaidRecords.empty());
}

TEST(HomePerimeterDomainTest, NewCycleReplacesOnlyLootStillOnTheGround)
{
    const ContentRegistry &content = publishedContentRegistry();
    ProfileState profile = makeNewPublishedProfile("perimeter-refresh", content);
    ASSERT_TRUE(ensureHomePerimeterSnapshot(
        profile, content, context(),
        {profile.revision, "generate-cycle-zero"}).succeeded);
    const HomePerimeterSiteSnapshot oldSnapshot =
        profile.homePerimeter.sites.at(kGreyline);
    ASSERT_GE(oldSnapshot.lootAssetIds.size(), 2U);
    const AssetInstanceId pickedUp = oldSnapshot.lootAssetIds.front();
    const AssetInstanceId leftBehind = oldSnapshot.lootAssetIds.back();
    AssetRecord *pickedUpAsset = profile.assets.findMutable(pickedUp);
    ASSERT_NE(pickedUpAsset, nullptr);
    pickedUpAsset->location = StoredAssetLocation{
        ProfileContainerId::stash(), {23, 15}};

    profile.worldClock.elapsedWorldMinutes = kHomePerimeterRefreshMinutes;
    const auto refreshed = ensureHomePerimeterSnapshot(
        profile, content, context(),
        {profile.revision, "generate-cycle-one"});
    ASSERT_TRUE(refreshed.succeeded) << refreshed.message;
    EXPECT_TRUE(refreshed.changed);
    EXPECT_NE(profile.assets.find(pickedUp), nullptr);
    EXPECT_EQ(profile.assets.find(leftBehind), nullptr);
    EXPECT_EQ(profile.homePerimeter.sites.at(kGreyline).cycleIndex, 1U);
}

TEST(HomePerimeterDomainTest, ActiveOutingPreventsCycleRefresh)
{
    const ContentRegistry &content = publishedContentRegistry();
    ProfileState profile = makeNewPublishedProfile("perimeter-active", content);
    ASSERT_TRUE(ensureHomePerimeterSnapshot(
        profile, content, context(),
        {profile.revision, "generate"}).succeeded);
    ASSERT_TRUE(beginHomePerimeterOuting(
        profile, kGreyline,
        {profile.revision, "outing"}).succeeded);
    const HomePerimeterSiteSnapshot before =
        profile.homePerimeter.sites.at(kGreyline);
    profile.worldClock.elapsedWorldMinutes = kHomePerimeterRefreshMinutes;

    const auto deferred = ensureHomePerimeterSnapshot(
        profile, content, context(),
        {profile.revision, "refresh-while-active"});
    EXPECT_TRUE(deferred.succeeded);
    EXPECT_FALSE(deferred.changed);
    EXPECT_EQ(profile.homePerimeter.sites.at(kGreyline), before);
}

TEST(HomePerimeterDomainTest, HistoricalLootIdMayDisappearAfterAStackMerge)
{
    const ContentRegistry &content = publishedContentRegistry();
    ProfileState profile = makeNewPublishedProfile("perimeter-merge", content);
    ASSERT_TRUE(ensureHomePerimeterSnapshot(
        profile, content, context(),
        {profile.revision, "generate"}).succeeded);
    const AssetInstanceId historical =
        profile.homePerimeter.sites.at(kGreyline).lootAssetIds.front();
    ASSERT_TRUE(profile.assets.erase(historical));

    const ProfileValidationResult validation =
        validateProfileState(profile, content);
    EXPECT_TRUE(validation.valid) << validation.message;
}

TEST(HomePerimeterDomainTest, StaleCommandCannotMutateProfile)
{
    const ContentRegistry &content = publishedContentRegistry();
    ProfileState profile = makeNewPublishedProfile("perimeter-stale", content);
    const std::uint64_t before = profileStateFingerprint(profile);
    const auto receipt = ensureHomePerimeterSnapshot(
        profile, content, context(),
        {profile.revision + 1U, "stale"});
    EXPECT_FALSE(receipt.succeeded);
    EXPECT_EQ(profileStateFingerprint(profile), before);
}
