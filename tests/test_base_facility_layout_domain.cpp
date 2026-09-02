#include <gtest/gtest.h>

#include <algorithm>
#include <utility>

#include "base_facility_layout_domain.h"

namespace
{
const RegionalBaseSiteDefinitionId kGreyline{
    "regional_base_site.greyline_yard"};
const BaseFacilityDefinitionId kWarehouse{"base_facility.warehouse"};
const BaseFacilityDefinitionId kWorkshop{"base_facility.workshop"};
const BaseFacilityDefinitionId kKitchenWater{
    "base_facility.kitchen_water"};

BaseFacilityLayoutAccess access(std::vector<ContentRect> blockers = {})
{
    return BaseFacilityLayoutAccess{
        kGreyline,
        ContentRect{{1000.0F, 2000.0F}, {1600.0F, 1120.0F}},
        std::move(blockers)};
}

bool contains(const ContentRect &outer, const ContentRect &inner)
{
    return inner.position.x >= outer.position.x &&
        inner.position.y >= outer.position.y &&
        inner.position.x + inner.size.x <=
            outer.position.x + outer.size.x &&
        inner.position.y + inner.size.y <=
            outer.position.y + outer.size.y;
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
     AccessGeometryIsDeterministicAndContainsTheSouthEntrance)
{
    const BaseFacilityAccessGeometry geometry =
        projectBaseFacilityAccessGeometry(
            {1400.0F, 2400.0F}, {300.0F, 220.0F});
    EXPECT_FLOAT_EQ(geometry.entrancePoint.x, 1400.0F);
    EXPECT_FLOAT_EQ(geometry.entrancePoint.y, 2528.0F);
    EXPECT_GT(geometry.interactionZone.size.x, 0.0F);
    EXPECT_GT(geometry.interactionZone.size.y, 0.0F);
    EXPECT_GE(
        geometry.entrancePoint.x,
        geometry.interactionZone.position.x);
    EXPECT_LE(
        geometry.entrancePoint.x,
        geometry.interactionZone.position.x +
            geometry.interactionZone.size.x);
    EXPECT_GE(
        geometry.entrancePoint.y,
        geometry.interactionZone.position.y);
    EXPECT_LE(
        geometry.entrancePoint.y,
        geometry.interactionZone.position.y +
            geometry.interactionZone.size.y);
    EXPECT_LE(
        geometry.workZone.position.x,
        geometry.interactionZone.position.x);
    EXPECT_LE(
        geometry.workZone.position.y,
        geometry.interactionZone.position.y);
    EXPECT_GE(
        geometry.workZone.position.x + geometry.workZone.size.x,
        geometry.interactionZone.position.x +
            geometry.interactionZone.size.x);
    EXPECT_GE(
        geometry.workZone.position.y + geometry.workZone.size.y,
        geometry.interactionZone.position.y +
            geometry.interactionZone.size.y);

    EXPECT_EQ(
        projectBaseFacilityAccessGeometry(
            {1400.0F, 2400.0F}, {300.0F, 220.0F}).entrancePoint.x,
        geometry.entrancePoint.x);
    EXPECT_EQ(
        projectBaseFacilityAccessGeometry({}, {}).workZone.size.x,
        0.0F);
}

TEST(BaseFacilityLayoutDomainTest,
     WorkSocketsAreStableTypedAndContainedByTheProtectedWorkZone)
{
    struct ExpectedSocket
    {
        BaseFacilityDefinitionId definitionId;
        BaseFacilityWorkSocketKind kind;
    };
    const std::array<ExpectedSocket, 5U> expected{{
        {BaseFacilityDefinitionId{"base_facility.warehouse"},
         BaseFacilityWorkSocketKind::StorageHandling},
        {BaseFacilityDefinitionId{"base_facility.medical"},
         BaseFacilityWorkSocketKind::MedicalBed},
        {BaseFacilityDefinitionId{"base_facility.dormitory"},
         BaseFacilityWorkSocketKind::DormitoryBunk},
        {BaseFacilityDefinitionId{"base_facility.kitchen_water"},
         BaseFacilityWorkSocketKind::KitchenProcessing},
        {BaseFacilityDefinitionId{"base_facility.workshop"},
         BaseFacilityWorkSocketKind::WorkshopBench},
    }};
    const Vec2 center{1400.0F, 2400.0F};
    const Vec2 footprint{300.0F, 220.0F};
    const BaseFacilityAccessGeometry accessGeometry =
        projectBaseFacilityAccessGeometry(center, footprint);

    for (const ExpectedSocket &candidate : expected)
    {
        const auto socket = projectBaseFacilityWorkSocket(
            candidate.definitionId, center, footprint);
        ASSERT_TRUE(socket.has_value());
        EXPECT_EQ(socket->kind, candidate.kind);
        EXPECT_TRUE(contains(accessGeometry.workZone, socket->bounds));
        EXPECT_FLOAT_EQ(
            socket->interactionPoint.x,
            socket->bounds.position.x + socket->bounds.size.x * 0.5F);
        EXPECT_FLOAT_EQ(
            socket->interactionPoint.y,
            socket->bounds.position.y + socket->bounds.size.y * 0.5F);
        const auto repeated = projectBaseFacilityWorkSocket(
            candidate.definitionId, center, footprint);
        ASSERT_TRUE(repeated.has_value());
        EXPECT_EQ(repeated->bounds, socket->bounds);
    }
    EXPECT_FALSE(projectBaseFacilityWorkSocket(
        BaseFacilityDefinitionId{"base_facility.unknown"},
        center, footprint).has_value());
    EXPECT_FALSE(projectBaseFacilityWorkSocket(
        kWorkshop, {}, {}).has_value());
}

TEST(BaseFacilityLayoutDomainTest,
     RepositionRejectsBlockedOrOutOfParcelWorkZoneWithoutMutation)
{
    const ContentRegistry &content = publishedContentRegistry();
    ProfileState profile = makeNewPublishedProfile(
        "facility-access-clearance", content);
    const std::uint64_t before = profileStateFingerprint(profile);

    const RepositionBaseFacilityCommand blockedAccess{
        kWarehouse,
        {1400.0F, 2400.0F},
        {300.0F, 220.0F},
        access({ContentRect{{1350.0F, 2550.0F}, {100.0F, 40.0F}}})};
    EXPECT_FALSE(queryBaseFacilityLayout(
        profile, content, blockedAccess).canCommit);
    EXPECT_FALSE(executeBaseFacilityLayout(
        profile, content, blockedAccess,
        CommandContext{profile.revision, "blocked-access"}).succeeded);
    EXPECT_EQ(profileStateFingerprint(profile), before);

    const RepositionBaseFacilityCommand outsideAccess{
        kWarehouse,
        {1400.0F, 2920.0F},
        {300.0F, 220.0F},
        access()};
    EXPECT_FALSE(queryBaseFacilityLayout(
        profile, content, outsideAccess).canCommit);
    EXPECT_FALSE(executeBaseFacilityLayout(
        profile, content, outsideAccess,
        CommandContext{profile.revision, "outside-access"}).succeeded);
    EXPECT_EQ(profileStateFingerprint(profile), before);
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
        EXPECT_FALSE(placements.contains(kKitchenWater));
    }

    profile.baseConstruction.kitchenWaterLevel = 1U;
    profile.baseConstruction.facilities[kKitchenWater] =
        BaseConstructionState::FacilityPlacement::Reserve;
    profile.baseConstruction.facilityReserveStartedWorldMinutes[
        kKitchenWater] = profile.worldClock.elapsedWorldMinutes;
    initializeBaseFacilityLayouts(profile, content);
    for (const auto &[site, placements] :
         profile.baseFacilityLayout.placements)
    {
        static_cast<void>(site);
        EXPECT_TRUE(placements.contains(kKitchenWater));
    }
}

TEST(BaseFacilityLayoutDomainTest,
     ReserveInstallationCommitsPlacementAndOperationalStateAtomically)
{
    const ContentRegistry &content = publishedContentRegistry();
    ProfileState profile = makeNewPublishedProfile(
        "facility-install-at", content);
    profile.worldClock.elapsedWorldMinutes += 180U;
    profile.baseConstruction.facilities[kWorkshop] =
        BaseConstructionState::FacilityPlacement::Reserve;
    profile.baseConstruction.facilityReserveStartedWorldMinutes[kWorkshop] =
        profile.worldClock.elapsedWorldMinutes - 120U;
    const InstallBaseFacilityAtCommand command{
        kWorkshop, {2200.0F, 2600.0F}, {270.0F, 170.0F}, access()};

    const std::uint64_t before = profileStateFingerprint(profile);
    const BaseFacilityLayoutPlan plan = queryInstallBaseFacilityAt(
        profile, content, command);
    ASSERT_TRUE(plan.canCommit) << plan.message;
    EXPECT_EQ(profileStateFingerprint(profile), before);

    const BaseFacilityLayoutReceipt receipt = executeInstallBaseFacilityAt(
        profile, content, command,
        CommandContext{profile.revision, "install-workshop-at"});
    ASSERT_TRUE(receipt.succeeded) << receipt.message;
    EXPECT_EQ(
        profile.baseConstruction.facilities.at(kWorkshop),
        BaseConstructionState::FacilityPlacement::Installed);
    EXPECT_FALSE(profile.baseConstruction.facilityReserveStartedWorldMinutes
                     .contains(kWorkshop));
    EXPECT_FLOAT_EQ(
        profile.baseFacilityLayout.placements.at(kGreyline).at(kWorkshop).x,
        0.75F);
    EXPECT_FLOAT_EQ(
        profile.baseFacilityLayout.placements.at(kGreyline).at(kWorkshop).y,
        600.0F / 1120.0F);

    const std::uint64_t committed = profileStateFingerprint(profile);
    const BaseFacilityLayoutReceipt repeated = executeInstallBaseFacilityAt(
        profile, content, command,
        CommandContext{profile.revision, "install-workshop-at"});
    EXPECT_TRUE(repeated.succeeded);
    EXPECT_TRUE(repeated.alreadyCommitted);
    EXPECT_EQ(profileStateFingerprint(profile), committed);
}

TEST(BaseFacilityLayoutDomainTest,
     BlockedReserveInstallationRejectsWithoutMutation)
{
    const ContentRegistry &content = publishedContentRegistry();
    ProfileState profile = makeNewPublishedProfile(
        "facility-install-blocked", content);
    profile.baseConstruction.facilities[kWorkshop] =
        BaseConstructionState::FacilityPlacement::Reserve;
    profile.baseConstruction.facilityReserveStartedWorldMinutes[kWorkshop] =
        profile.worldClock.elapsedWorldMinutes;
    const InstallBaseFacilityAtCommand command{
        kWorkshop,
        {2200.0F, 2600.0F},
        {270.0F, 170.0F},
        access({ContentRect{{2100.0F, 2520.0F}, {200.0F, 160.0F}}})};
    const std::uint64_t before = profileStateFingerprint(profile);

    EXPECT_FALSE(queryInstallBaseFacilityAt(
        profile, content, command).canCommit);
    EXPECT_FALSE(executeInstallBaseFacilityAt(
        profile, content, command,
        CommandContext{profile.revision, "blocked-install"}).succeeded);
    EXPECT_EQ(profileStateFingerprint(profile), before);
}
