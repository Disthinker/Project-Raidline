#include <array>
#include <algorithm>

#include <gtest/gtest.h>

#include "base_facility_management.h"
#include "inventory_domain.h"

namespace
{
const BaseFacilityQuickActionProjection *findAction(
    const BaseFacilityManagementProjection &projection,
    BaseFacilityQuickActionKind kind)
{
    const auto found = std::find_if(
        projection.quickActions.begin(), projection.quickActions.end(),
        [kind](const BaseFacilityQuickActionProjection &action)
        {
            return action.kind == kind;
        });
    return found == projection.quickActions.end() ? nullptr : &*found;
}
}

TEST(BaseFacilityManagementTest,
     ProjectsAllFixedFacilitiesWithoutMutatingProfile)
{
    ProfileState profile = makeNewAlphaProfile(
        "facility-management-default", publishedContentRegistry());
    const std::uint64_t fingerprint = profileStateFingerprint(profile);
    constexpr std::array<BaseFacilityKind, 8U> kinds{
        BaseFacilityKind::Storage,
        BaseFacilityKind::Supply,
        BaseFacilityKind::Allocation,
        BaseFacilityKind::Medical,
        BaseFacilityKind::Dormitory,
        BaseFacilityKind::KitchenWater,
        BaseFacilityKind::Workshop,
        BaseFacilityKind::RaidGate};

    for (const BaseFacilityKind kind : kinds)
    {
        const BaseFacilityManagementProjection projection =
            projectBaseFacilityManagement(
                profile, publishedContentRegistry(), kind);
        EXPECT_EQ(projection.kind, kind);
        EXPECT_EQ(
            projection.status,
            kind == BaseFacilityKind::KitchenWater
                ? BaseFacilityOperationalStatus::Unavailable
                : BaseFacilityOperationalStatus::Operational);
        EXPECT_EQ(projection.task, BaseFacilityTaskKind::Idle);
    }
    EXPECT_TRUE(projectBaseFacilityManagement(
        profile, publishedContentRegistry(), BaseFacilityKind::Dormitory)
                    .level.has_value());
    EXPECT_EQ(
        projectBaseFacilityManagement(
            profile, publishedContentRegistry(),
            BaseFacilityKind::KitchenWater).level,
        std::optional<std::uint32_t>{0U});
    EXPECT_TRUE(projectBaseFacilityManagement(
        profile, publishedContentRegistry(), BaseFacilityKind::Workshop)
                    .staffingApplicable);
    EXPECT_FALSE(projectBaseFacilityManagement(
        profile, publishedContentRegistry(), BaseFacilityKind::Supply)
                     .staffingApplicable);
    EXPECT_EQ(profileStateFingerprint(profile), fingerprint);
}

TEST(BaseFacilityManagementTest,
     PublishesContextActionsWithoutChoosingManufacturingRecipe)
{
    const ProfileState profile = makeNewAlphaProfile(
        "facility-management-actions", publishedContentRegistry());

    const BaseFacilityManagementProjection workshop =
        projectBaseFacilityManagement(
            profile, publishedContentRegistry(), BaseFacilityKind::Workshop);
    EXPECT_NE(findAction(
        workshop, BaseFacilityQuickActionKind::OpenFunction), nullptr);
    EXPECT_NE(findAction(
        workshop, BaseFacilityQuickActionKind::ClearWorker), nullptr);
    EXPECT_NE(findAction(
        workshop, BaseFacilityQuickActionKind::StartUpgrade), nullptr);
    EXPECT_EQ(findAction(
        workshop, BaseFacilityQuickActionKind::CollectManufacturing), nullptr);
    EXPECT_EQ(findAction(
        workshop, BaseFacilityQuickActionKind::CancelManufacturing), nullptr);

    const BaseFacilityManagementProjection medical =
        projectBaseFacilityManagement(
            profile, publishedContentRegistry(), BaseFacilityKind::Medical);
    const BaseFacilityQuickActionProjection *treatment = findAction(
        medical, BaseFacilityQuickActionKind::StartResidentTreatment);
    ASSERT_NE(treatment, nullptr);
    EXPECT_FALSE(treatment->canCommit);

    const BaseFacilityManagementProjection dormitory =
        projectBaseFacilityManagement(
            profile, publishedContentRegistry(), BaseFacilityKind::Dormitory);
    EXPECT_NE(findAction(
        dormitory, BaseFacilityQuickActionKind::AutoFillWorkers), nullptr);
}

TEST(BaseFacilityManagementTest,
     ReportsConstructionBeforePausedFacilityWork)
{
    ProfileState profile = makeNewAlphaProfile(
        "facility-management-construction", publishedContentRegistry());
    profile.worldClock.elapsedWorldMinutes = 1000U;
    profile.baseConstruction.workshopLevel = 1U;
    profile.baseConstruction.activeProject = ActiveBaseConstructionProject{
        BaseConstructionProjectDefinitionId{
            "base_construction.workshop.level_2"},
        4U,
        2U,
        1000U,
        1360U};
    profile.baseManufacturing.activeOrder = BaseManufacturingOrder{
        BaseServiceJobId{11U},
        BaseManufacturingRecipeDefinitionId{
            "base_manufacturing.weapon_maintenance_kit"},
        1U,
        BaseResidentProfession::Engineering,
        900U,
        1200U,
        {},
        AssetInstanceId{99U},
        false};

    const BaseFacilityManagementProjection projection =
        projectBaseFacilityManagement(
            profile, publishedContentRegistry(), BaseFacilityKind::Workshop);
    ASSERT_TRUE(projection.level.has_value());
    EXPECT_EQ(*projection.level, 1U);
    EXPECT_EQ(projection.task, BaseFacilityTaskKind::Construction);
    EXPECT_EQ(projection.remainingMinutes, 360U);
    ASSERT_TRUE(projection.assignedWorker.has_value());
    EXPECT_EQ(
        *projection.assignedWorker,
        BaseResidentProfession::Engineering);
    const BaseFacilityQuickActionProjection *cancelUpgrade = findAction(
        projection, BaseFacilityQuickActionKind::CancelUpgrade);
    ASSERT_NE(cancelUpgrade, nullptr);
    ASSERT_TRUE(cancelUpgrade->constructionProjectId.has_value());
    EXPECT_EQ(
        cancelUpgrade->constructionProjectId->value(),
        "base_construction.workshop.level_2");
    EXPECT_NE(findAction(
        projection, BaseFacilityQuickActionKind::CancelManufacturing), nullptr);
}

TEST(BaseFacilityManagementTest,
     FirstKitchenConstructionIsActiveBeforeFacilityOwnership)
{
    ProfileState profile = makeNewAlphaProfile(
        "facility-management-kitchen-build", publishedContentRegistry());
    profile.worldClock.elapsedWorldMinutes = 1000U;
    profile.baseConstruction.activeProject = ActiveBaseConstructionProject{
        BaseConstructionProjectDefinitionId{
            "base_construction.kitchen_water.level_1"},
        5U,
        2U,
        1000U,
        1480U};

    const BaseOperationsOverviewProjection overview =
        projectBaseOperationsOverview(
            profile, publishedContentRegistry());
    const auto found = std::find_if(
        overview.entries.begin(), overview.entries.end(),
        [](const BaseOperationOverviewEntry &entry)
        {
            return entry.facility == BaseFacilityKind::KitchenWater &&
                entry.kind == BaseOperationOverviewKind::Construction;
        });
    ASSERT_NE(found, overview.entries.end());
    EXPECT_FALSE(found->paused);
    EXPECT_EQ(found->remainingMinutes, 480U);
}

TEST(BaseFacilityManagementTest,
     ReportsManufacturingOutputAndResidentTreatment)
{
    ProfileState profile = makeNewAlphaProfile(
        "facility-management-jobs", publishedContentRegistry());
    profile.worldClock.elapsedWorldMinutes = 1000U;
    profile.baseManufacturing.activeOrder = BaseManufacturingOrder{
        BaseServiceJobId{21U},
        BaseManufacturingRecipeDefinitionId{
            "base_manufacturing.weapon_maintenance_kit"},
        1U,
        BaseResidentProfession::Engineering,
        900U,
        1180U,
        {},
        AssetInstanceId{88U},
        false};
    profile.residentMedical.activeTreatment = ActiveResidentTreatment{
        BaseServiceJobId{22U},
        950U,
        1120U,
        2U,
        BaseResidentProfession::General,
        BaseResidentProfession::Medical};

    BaseFacilityManagementProjection workshop =
        projectBaseFacilityManagement(
            profile, publishedContentRegistry(), BaseFacilityKind::Workshop);
    EXPECT_EQ(workshop.task, BaseFacilityTaskKind::Manufacturing);
    EXPECT_EQ(workshop.remainingMinutes, 180U);

    const BaseFacilityManagementProjection medical =
        projectBaseFacilityManagement(
            profile, publishedContentRegistry(), BaseFacilityKind::Medical);
    EXPECT_EQ(medical.task, BaseFacilityTaskKind::ResidentTreatment);
    EXPECT_EQ(medical.remainingMinutes, 120U);

    profile.baseManufacturing.activeOrder->outputReady = true;
    workshop = projectBaseFacilityManagement(
        profile, publishedContentRegistry(), BaseFacilityKind::Workshop);
    EXPECT_EQ(workshop.task, BaseFacilityTaskKind::OutputReady);
    EXPECT_EQ(workshop.remainingMinutes, 0U);
    EXPECT_NE(findAction(
        workshop, BaseFacilityQuickActionKind::CollectManufacturing), nullptr);
    EXPECT_EQ(findAction(
        workshop, BaseFacilityQuickActionKind::CancelManufacturing), nullptr);
}

TEST(BaseFacilityManagementTest, ReportsOwnedFacilityInReserve)
{
    ProfileState profile = makeNewAlphaProfile(
        "facility-management-reserve", publishedContentRegistry());
    profile.baseConstruction.facilities.at(
        BaseFacilityDefinitionId{"base_facility.workshop"}) =
        BaseConstructionState::FacilityPlacement::Reserve;

    const BaseFacilityManagementProjection projection =
        projectBaseFacilityManagement(
            profile, publishedContentRegistry(), BaseFacilityKind::Workshop);
    EXPECT_EQ(
        projection.status,
        BaseFacilityOperationalStatus::Reserve);
    ASSERT_EQ(projection.quickActions.size(), 1U);
    EXPECT_EQ(
        projection.quickActions.front().kind,
        BaseFacilityQuickActionKind::OpenFunction);
    EXPECT_FALSE(projection.quickActions.front().canCommit);
}

TEST(BaseFacilityManagementTest,
     OperationsOverviewPrioritizesReadyAndActiveWorkWithoutMutation)
{
    ProfileState profile = makeNewAlphaProfile(
        "facility-operations-overview", publishedContentRegistry());
    profile.worldClock.elapsedWorldMinutes = 1000U;
    profile.baseConstruction.activeProject = ActiveBaseConstructionProject{
        BaseConstructionProjectDefinitionId{
            "base_construction.dormitory.level_2"},
        3U,
        1U,
        900U,
        1300U};
    profile.baseManufacturing.activeOrder = BaseManufacturingOrder{
        BaseServiceJobId{41U},
        BaseManufacturingRecipeDefinitionId{
            "base_manufacturing.weapon_maintenance_kit"},
        1U,
        BaseResidentProfession::Engineering,
        800U,
        900U,
        {},
        AssetInstanceId{991U},
        true};
    profile.residentMedical.activeTreatment = ActiveResidentTreatment{
        BaseServiceJobId{42U},
        940U,
        1120U,
        2U,
        BaseResidentProfession::General,
        BaseResidentProfession::Medical};
    profile.baseWorkforce.workshopWorker.reset();
    const std::uint64_t fingerprint = profileStateFingerprint(profile);

    const BaseOperationsOverviewProjection projection =
        projectBaseOperationsOverview(
            profile, publishedContentRegistry());

    ASSERT_EQ(projection.entries.size(), 4U);
    EXPECT_EQ(
        projection.entries[0].kind,
        BaseOperationOverviewKind::OutputReady);
    EXPECT_EQ(
        projection.entries[0].facility,
        BaseFacilityKind::Workshop);
    EXPECT_EQ(
        projection.entries[1].kind,
        BaseOperationOverviewKind::Construction);
    EXPECT_EQ(
        projection.entries[1].facility,
        BaseFacilityKind::Dormitory);
    EXPECT_EQ(projection.entries[1].remainingMinutes, 300U);
    EXPECT_EQ(
        projection.entries[2].kind,
        BaseOperationOverviewKind::ResidentTreatment);
    EXPECT_EQ(projection.entries[2].remainingMinutes, 120U);
    EXPECT_EQ(
        projection.entries[3].kind,
        BaseOperationOverviewKind::StaffingGap);
    EXPECT_EQ(
        projection.entries[3].facility,
        BaseFacilityKind::Workshop);
    EXPECT_EQ(profileStateFingerprint(profile), fingerprint);
}

TEST(BaseFacilityManagementTest,
     OperationsOverviewReportsPausedReserveWorkAndHealthyEmptyState)
{
    ProfileState profile = makeNewAlphaProfile(
        "facility-operations-paused", publishedContentRegistry());
    profile.baseConstruction.facilities.at(
        BaseFacilityDefinitionId{"base_facility.workshop"}) =
        BaseConstructionState::FacilityPlacement::Reserve;
    profile.baseManufacturing.activeOrder = BaseManufacturingOrder{
        BaseServiceJobId{51U},
        BaseManufacturingRecipeDefinitionId{
            "base_manufacturing.weapon_maintenance_kit"},
        1U,
        BaseResidentProfession::Engineering,
        0U,
        100U,
        {},
        AssetInstanceId{992U},
        false};

    BaseOperationsOverviewProjection projection =
        projectBaseOperationsOverview(
            profile, publishedContentRegistry());
    ASSERT_EQ(projection.entries.size(), 1U);
    EXPECT_EQ(
        projection.entries.front().kind,
        BaseOperationOverviewKind::Manufacturing);
    EXPECT_TRUE(projection.entries.front().paused);

    profile.baseManufacturing.activeOrder.reset();
    profile.baseConstruction.facilities.at(
        BaseFacilityDefinitionId{"base_facility.workshop"}) =
        BaseConstructionState::FacilityPlacement::Installed;
    projection = projectBaseOperationsOverview(
        profile, publishedContentRegistry());
    EXPECT_TRUE(projection.entries.empty());
}
