#include <gtest/gtest.h>

#include "base_workforce_domain.h"

namespace
{
ProfileState makeProfile()
{
    return makeNewAlphaProfile(
        "base-workforce-test",
        publishedContentRegistry());
}
}

TEST(BaseWorkforceDomainTest, InitialProfessionalsFillBothFacilitySlots)
{
    const ProfileState profile = makeProfile();
    const BaseWorkforceProjection projection = projectBaseWorkforce(profile);

    ASSERT_TRUE(projection.workshopWorker.has_value());
    ASSERT_TRUE(projection.medicalWorker.has_value());
    EXPECT_EQ(*projection.workshopWorker, BaseResidentProfession::Engineering);
    EXPECT_EQ(*projection.medicalWorker, BaseResidentProfession::Medical);
    EXPECT_EQ(projection.assignedResidents, 2U);
    EXPECT_EQ(projection.availableResidents, 6U);
}

TEST(BaseWorkforceDomainTest, BestAssignmentPrefersSpecialistThenGeneralFallback)
{
    ProfileState profile = makeProfile();
    ASSERT_TRUE(executeClearBaseWorker(
        profile,
        publishedContentRegistry(),
        BaseFacilityStaffingCommand{BaseFacilityStaffingKind::Workshop},
        CommandContext{profile.revision, "clear-workshop"}).succeeded);
    ASSERT_TRUE(executeAssignBestBaseWorker(
        profile,
        publishedContentRegistry(),
        BaseFacilityStaffingCommand{BaseFacilityStaffingKind::Workshop},
        CommandContext{profile.revision, "assign-workshop"}).succeeded);
    ASSERT_TRUE(profile.baseWorkforce.workshopWorker.has_value());
    EXPECT_EQ(
        *profile.baseWorkforce.workshopWorker,
        BaseResidentProfession::Engineering);

    ASSERT_TRUE(executeClearBaseWorker(
        profile,
        publishedContentRegistry(),
        BaseFacilityStaffingCommand{BaseFacilityStaffingKind::Workshop},
        CommandContext{profile.revision, "clear-workshop-again"}).succeeded);
    --profile.basePopulation.professionResidents[baseProfessionIndex(
        BaseResidentProfession::Engineering)];
    ++profile.basePopulation.professionResidents[baseProfessionIndex(
        BaseResidentProfession::General)];
    ASSERT_TRUE(validateProfileState(profile, publishedContentRegistry()).valid);

    const BaseWorkforceReceipt fallback = executeAssignBestBaseWorker(
        profile,
        publishedContentRegistry(),
        BaseFacilityStaffingCommand{BaseFacilityStaffingKind::Workshop},
        CommandContext{profile.revision, "assign-general-fallback"});
    ASSERT_TRUE(fallback.succeeded) << fallback.message;
    ASSERT_TRUE(profile.baseWorkforce.workshopWorker.has_value());
    EXPECT_EQ(
        *profile.baseWorkforce.workshopWorker,
        BaseResidentProfession::General);
}

TEST(BaseWorkforceDomainTest, AutoFillIsAtomicAndClearingActiveJobIsRejected)
{
    ProfileState profile = makeProfile();
    profile.baseWorkforce.workshopWorker.reset();
    profile.baseWorkforce.medicalWorker.reset();
    const BaseWorkforceReceipt filled = executeAutoFillBaseWorkers(
        profile,
        publishedContentRegistry(),
        CommandContext{profile.revision, "autofill"});
    ASSERT_TRUE(filled.succeeded) << filled.message;
    EXPECT_EQ(
        profile.baseWorkforce.workshopWorker,
        BaseResidentProfession::Engineering);
    EXPECT_EQ(
        profile.baseWorkforce.medicalWorker,
        BaseResidentProfession::Medical);

    profile.baseManufacturing.activeOrder = BaseManufacturingOrder{
        1U,
        BaseManufacturingRecipeDefinitionId{
            "base_manufacturing.weapon_maintenance_kit"},
        1U,
        BaseResidentProfession::Engineering,
        profile.worldClock.elapsedWorldMinutes,
        profile.worldClock.elapsedWorldMinutes + 360U,
        {},
        1U,
        false};
    const std::uint64_t before = profileStateFingerprint(profile);
    const BaseWorkforcePlan plan = queryClearBaseWorker(
        profile,
        BaseFacilityStaffingCommand{BaseFacilityStaffingKind::Workshop});
    EXPECT_FALSE(plan.canCommit);
    EXPECT_EQ(profileStateFingerprint(profile), before);
}

TEST(BaseWorkforceDomainTest, DurationCombinesProfessionAndFacilityLevel)
{
    const BaseWorkforceDefinition &definition =
        publishedContentRegistry().baseWorkforce();
    EXPECT_EQ(applyBaseFacilityTaskDuration(
        360U,
        BaseFacilityStaffingKind::Workshop,
        BaseResidentProfession::Engineering,
        1U,
        definition), 360U);
    EXPECT_EQ(applyBaseFacilityTaskDuration(
        360U,
        BaseFacilityStaffingKind::Workshop,
        BaseResidentProfession::General,
        1U,
        definition), 540U);
    EXPECT_EQ(applyBaseFacilityTaskDuration(
        360U,
        BaseFacilityStaffingKind::Workshop,
        BaseResidentProfession::Engineering,
        2U,
        definition), 306U);
    EXPECT_EQ(applyBaseFacilityTaskDuration(
        360U,
        BaseFacilityStaffingKind::Medical,
        BaseResidentProfession::General,
        2U,
        definition), 459U);
}

TEST(BaseWorkforceDomainTest,
     RegionalGarrisonIsIncludedInSharedWorkforceProjection)
{
    ProfileState profile = makeProfile();
    const RegionalOutpostDefinitionId outpostId{
        "regional_outpost.old_service_relay"};
    RegionalOutpostState &outpost =
        profile.regionalOperations.outposts.at(outpostId);
    outpost.established = true;
    outpost.assignedStaff[baseProfessionIndex(
        BaseResidentProfession::General)] = 2U;

    const BaseWorkforceProjection projection =
        projectBaseWorkforce(profile);

    EXPECT_EQ(projection.assignedResidents, 4U);
    EXPECT_EQ(projection.availableResidents, 4U);
    EXPECT_EQ(
        projection.availableByProfession[baseProfessionIndex(
            BaseResidentProfession::General)],
        4U);
}
