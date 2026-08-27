#include <gtest/gtest.h>

#include "base_site_feature_domain.h"

namespace
{
const RegionalBaseSiteDefinitionId kGreylineSite{
    "regional_base_site.greyline_yard"};
const RegionalBaseSiteDefinitionId kAshworksSite{
    "regional_base_site.ashworks_logistics_yard"};
const RegionalOutpostDefinitionId kAshworksOutpost{
    "regional_outpost.ashworks_logistics_yard"};

ProfileState ashworksProfile()
{
    ProfileState profile = makeNewAlphaProfile(
        "base-site-feature-test", publishedContentRegistry());
    profile.regionalOperations.baseSites.at(kAshworksSite).unlocked = true;
    profile.regionalOperations.outposts.at(kAshworksOutpost).unlocked = true;
    profile.regionalOperations.activeBaseNodeId =
        RegionNodeDefinitionId{"region_node.base.ashworks_logistics_yard"};
    profile.regionalOperations.technologyCore.baseSiteDefinitionId =
        kAshworksSite;
    profile.baseConstruction.materialUnits = 20U;
    return profile;
}
}

TEST(BaseSiteFeatureDomainTest,
     QueryRequiresActiveSiteWorkshopMaterialAndAvailableWorker)
{
    const ContentRegistry &content = publishedContentRegistry();
    ProfileState profile = makeNewAlphaProfile("feature-query", content);
    EXPECT_FALSE(queryBaseSiteFeatureRepair(
        profile, content, BaseSiteFeatureRepairCommand{kAshworksSite})
        .canCommit);

    profile = ashworksProfile();
    const BaseSiteFeatureRepairPlan ready = queryBaseSiteFeatureRepair(
        profile, content, BaseSiteFeatureRepairCommand{kAshworksSite});
    ASSERT_TRUE(ready.canCommit) << ready.message;
    EXPECT_EQ(ready.materialUnits, 15U);
    EXPECT_EQ(ready.workerCount, 1U);
    EXPECT_EQ(ready.durationMinutes, 360U);
    EXPECT_EQ(ready.manufacturingDurationPercent, 75U);

    profile.baseConstruction.materialUnits = 14U;
    EXPECT_EQ(queryBaseSiteFeatureRepair(
        profile, content, BaseSiteFeatureRepairCommand{kAshworksSite}).error,
        DomainErrorCode::Capacity);

    profile = ashworksProfile();
    profile.baseConstruction.facilities.at(
        BaseFacilityDefinitionId{"base_facility.workshop"}) =
        BaseConstructionState::FacilityPlacement::Reserve;
    profile.baseConstruction.facilityReserveStartedWorldMinutes.emplace(
        BaseFacilityDefinitionId{"base_facility.workshop"},
        profile.worldClock.elapsedWorldMinutes);
    EXPECT_EQ(queryBaseSiteFeatureRepair(
        profile, content, BaseSiteFeatureRepairCommand{kAshworksSite}).error,
        DomainErrorCode::IllegalDestination);
}

TEST(BaseSiteFeatureDomainTest,
     RepairIsAtomicAdvancesSchedulesAndIsIdempotent)
{
    const ContentRegistry &content = publishedContentRegistry();
    ProfileState profile = ashworksProfile();
    const ProfileRevision beforeRevision = profile.revision;
    const std::uint64_t beforeMinute = profile.worldClock.elapsedWorldMinutes;

    const BaseSiteFeatureRepairReceipt repaired =
        executeBaseSiteFeatureRepair(
            profile,
            content,
            BaseSiteFeatureRepairCommand{kAshworksSite},
            CommandContext{profile.revision, "repair-ashworks-feature"});
    ASSERT_TRUE(repaired.succeeded) << repaired.message;
    EXPECT_FALSE(repaired.alreadyCommitted);
    EXPECT_EQ(repaired.materialUnitsConsumed, 15U);
    EXPECT_EQ(repaired.worldMinutesApplied, 360U);
    EXPECT_EQ(profile.baseConstruction.materialUnits, 5U);
    EXPECT_EQ(profile.worldClock.elapsedWorldMinutes, beforeMinute + 360U);
    EXPECT_EQ(profile.revision, beforeRevision + 1U);
    EXPECT_TRUE(profile.regionalOperations.baseSites.at(kAshworksSite)
                    .uniqueFeatureRepaired);
    EXPECT_TRUE(validateProfileState(profile, content).valid);

    const std::uint64_t fingerprint = profileStateFingerprint(profile);
    const BaseSiteFeatureRepairReceipt replay =
        executeBaseSiteFeatureRepair(
            profile,
            content,
            BaseSiteFeatureRepairCommand{kAshworksSite},
            CommandContext{profile.revision, "repair-ashworks-feature"});
    EXPECT_TRUE(replay.succeeded);
    EXPECT_TRUE(replay.alreadyCommitted);
    EXPECT_EQ(profileStateFingerprint(profile), fingerprint);
}

TEST(BaseSiteFeatureDomainTest,
     EstablishedStagingOutpostAllowsRepairBeforeMigration)
{
    const ContentRegistry &content = publishedContentRegistry();
    ProfileState profile = makeNewAlphaProfile(
        "remote-site-feature-test", content);
    profile.regionalOperations.baseSites.at(kAshworksSite).unlocked = true;
    RegionalOutpostState &outpost =
        profile.regionalOperations.outposts.at(kAshworksOutpost);
    outpost.unlocked = true;
    outpost.established = true;
    profile.baseConstruction.materialUnits = 20U;

    const BaseSiteFeatureRepairPlan plan = queryBaseSiteFeatureRepair(
        profile, content, BaseSiteFeatureRepairCommand{kAshworksSite});
    ASSERT_TRUE(plan.canCommit) << plan.message;
    const BaseSiteFeatureRepairReceipt repaired = executeBaseSiteFeatureRepair(
        profile,
        content,
        BaseSiteFeatureRepairCommand{kAshworksSite},
        CommandContext{profile.revision, "repair-staged-ashworks"});
    ASSERT_TRUE(repaired.succeeded) << repaired.message;
    EXPECT_TRUE(profile.regionalOperations.baseSites.at(kAshworksSite)
                    .uniqueFeatureRepaired);
    EXPECT_EQ(activeBaseSiteManufacturingDurationPercent(profile, content),
              100U);
}

TEST(BaseSiteFeatureDomainTest,
     RejectionsAndStaleRevisionLeaveProfileUnchanged)
{
    const ContentRegistry &content = publishedContentRegistry();
    ProfileState profile = ashworksProfile();
    profile.baseConstruction.materialUnits = 0U;
    const std::uint64_t before = profileStateFingerprint(profile);
    const BaseSiteFeatureRepairReceipt missingMaterial =
        executeBaseSiteFeatureRepair(
            profile,
            content,
            BaseSiteFeatureRepairCommand{kAshworksSite},
            CommandContext{profile.revision, "repair-no-material"});
    EXPECT_FALSE(missingMaterial.succeeded);
    EXPECT_EQ(profileStateFingerprint(profile), before);

    profile = ashworksProfile();
    const std::uint64_t staleBefore = profileStateFingerprint(profile);
    const BaseSiteFeatureRepairReceipt stale = executeBaseSiteFeatureRepair(
        profile,
        content,
        BaseSiteFeatureRepairCommand{kAshworksSite},
        CommandContext{profile.revision + 1U, "repair-stale"});
    EXPECT_FALSE(stale.succeeded);
    EXPECT_EQ(stale.error, DomainErrorCode::StaleRevision);
    EXPECT_EQ(profileStateFingerprint(profile), staleBefore);
}

TEST(BaseSiteFeatureDomainTest,
     ManufacturingModifierOnlyOperatesAtTheRepairedActiveSite)
{
    const ContentRegistry &content = publishedContentRegistry();
    ProfileState profile = ashworksProfile();
    EXPECT_EQ(activeBaseSiteManufacturingDurationPercent(profile, content),
              100U);
    profile.regionalOperations.baseSites.at(kAshworksSite)
        .uniqueFeatureRepaired = true;
    EXPECT_EQ(activeBaseSiteManufacturingDurationPercent(profile, content),
              75U);
    EXPECT_EQ(applyActiveBaseSiteManufacturingDuration(
                  361U, profile, content),
              271U);

    profile.regionalOperations.activeBaseNodeId =
        RegionNodeDefinitionId{"region_node.base.greyline_yard"};
    profile.regionalOperations.technologyCore.baseSiteDefinitionId =
        kGreylineSite;
    EXPECT_EQ(activeBaseSiteManufacturingDurationPercent(profile, content),
              100U);
}
