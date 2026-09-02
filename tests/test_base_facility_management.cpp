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
        workshop, BaseFacilityQuickActionKind::AutoFillWorkers), nullptr);
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
    EXPECT_NE(findAction(
        medical, BaseFacilityQuickActionKind::AutoFillWorkers), nullptr);

    const BaseFacilityManagementProjection dormitory =
        projectBaseFacilityManagement(
            profile, publishedContentRegistry(), BaseFacilityKind::Dormitory);
    EXPECT_NE(findAction(
        dormitory, BaseFacilityQuickActionKind::AutoFillWorkers), nullptr);
    EXPECT_EQ(
        std::count_if(
            dormitory.quickActions.begin(), dormitory.quickActions.end(),
            [](const BaseFacilityQuickActionProjection &action)
            {
                return action.kind ==
                    BaseFacilityQuickActionKind::AutoFillWorkers;
            }),
        1);
}

TEST(BaseFacilityManagementTest,
     WorkforceQuickActionsUseDomainPlansAndRemainPure)
{
    ProfileState profile = makeNewAlphaProfile(
        "facility-workforce-actions", publishedContentRegistry());
    const std::uint64_t initialFingerprint =
        profileStateFingerprint(profile);

    BaseFacilityManagementProjection workshop =
        projectBaseFacilityManagement(
            profile, publishedContentRegistry(), BaseFacilityKind::Workshop);
    const BaseFacilityQuickActionProjection *clear = findAction(
        workshop, BaseFacilityQuickActionKind::ClearWorker);
    const BaseFacilityQuickActionProjection *autoFill = findAction(
        workshop, BaseFacilityQuickActionKind::AutoFillWorkers);
    ASSERT_NE(clear, nullptr);
    ASSERT_NE(autoFill, nullptr);
    EXPECT_TRUE(clear->canCommit);
    EXPECT_FALSE(autoFill->canCommit);
    EXPECT_EQ(profileStateFingerprint(profile), initialFingerprint);

    profile.baseWorkforce.workshopWorker.reset();
    const std::uint64_t missingFingerprint =
        profileStateFingerprint(profile);
    workshop = projectBaseFacilityManagement(
        profile, publishedContentRegistry(), BaseFacilityKind::Workshop);
    const BaseFacilityQuickActionProjection *assign = findAction(
        workshop, BaseFacilityQuickActionKind::AssignBestWorker);
    autoFill = findAction(
        workshop, BaseFacilityQuickActionKind::AutoFillWorkers);
    ASSERT_NE(assign, nullptr);
    ASSERT_NE(autoFill, nullptr);
    EXPECT_TRUE(assign->canCommit);
    EXPECT_TRUE(autoFill->canCommit);
    EXPECT_EQ(profileStateFingerprint(profile), missingFingerprint);
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

TEST(BaseFacilityManagementTest,
     WorldServiceProjectionUsesStablePlayerFacingPriority)
{
    ProfileState profile = makeNewAlphaProfile(
        "facility-world-service-priority", publishedContentRegistry());
    profile.worldClock.elapsedWorldMinutes = 1000U;

    const BaseFacilityWorldServiceProjection storage =
        projectBaseFacilityWorldService(
            profile, publishedContentRegistry(), BaseFacilityKind::Storage);
    EXPECT_EQ(storage.status, BaseFacilityWorldServiceStatus::Ready);
    EXPECT_EQ(storage.task, BaseFacilityTaskKind::Idle);

    profile.baseWorkforce.workshopWorker.reset();
    BaseFacilityWorldServiceProjection workshop =
        projectBaseFacilityWorldService(
            profile, publishedContentRegistry(), BaseFacilityKind::Workshop);
    EXPECT_EQ(workshop.status, BaseFacilityWorldServiceStatus::NeedsStaff);
    EXPECT_FALSE(workshop.activeWorkSocket.has_value());

    profile.baseConstruction.activeProject = ActiveBaseConstructionProject{
        BaseConstructionProjectDefinitionId{
            "base_construction.workshop.level_2"},
        3U,
        1U,
        1000U,
        1120U};
    workshop = projectBaseFacilityWorldService(
        profile, publishedContentRegistry(), BaseFacilityKind::Workshop);
    EXPECT_EQ(workshop.status, BaseFacilityWorldServiceStatus::Working);
    EXPECT_EQ(workshop.task, BaseFacilityTaskKind::Construction);
    EXPECT_EQ(
        workshop.activeWorkSocket,
        BaseFacilityWorkSocketKind::WorkshopBench);
    EXPECT_EQ(workshop.remainingMinutes, 120U);
    profile.baseConstruction.activeProject.reset();

    profile.baseManufacturing.activeOrder = BaseManufacturingOrder{
        BaseServiceJobId{61U},
        BaseManufacturingRecipeDefinitionId{
            "base_manufacturing.weapon_maintenance_kit"},
        1U,
        BaseResidentProfession::Engineering,
        900U,
        1180U,
        {},
        AssetInstanceId{993U},
        false};
    workshop = projectBaseFacilityWorldService(
        profile, publishedContentRegistry(), BaseFacilityKind::Workshop);
    EXPECT_EQ(workshop.status, BaseFacilityWorldServiceStatus::Working);
    EXPECT_EQ(workshop.task, BaseFacilityTaskKind::Manufacturing);
    EXPECT_EQ(
        workshop.activeWorkSocket,
        BaseFacilityWorkSocketKind::WorkshopBench);
    EXPECT_EQ(workshop.remainingMinutes, 180U);

    profile.baseManufacturing.activeOrder->outputReady = true;
    workshop = projectBaseFacilityWorldService(
        profile, publishedContentRegistry(), BaseFacilityKind::Workshop);
    EXPECT_EQ(workshop.status, BaseFacilityWorldServiceStatus::OutputReady);
    EXPECT_EQ(
        workshop.activeWorkSocket,
        BaseFacilityWorkSocketKind::WorkshopBench);
    EXPECT_EQ(workshop.remainingMinutes, 0U);

    profile.baseConstruction.facilities.at(
        BaseFacilityDefinitionId{"base_facility.workshop"}) =
        BaseConstructionState::FacilityPlacement::Reserve;
    workshop = projectBaseFacilityWorldService(
        profile, publishedContentRegistry(), BaseFacilityKind::Workshop);
    EXPECT_EQ(workshop.status, BaseFacilityWorldServiceStatus::Blocked);
    EXPECT_FALSE(workshop.activeWorkSocket.has_value());
}

TEST(BaseFacilityManagementTest,
     WorldServiceProjectionIsPureForEveryFacility)
{
    ProfileState profile = makeNewAlphaProfile(
        "facility-world-service-pure", publishedContentRegistry());
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
        const BaseFacilityWorldServiceProjection projection =
            projectBaseFacilityWorldService(
                profile, publishedContentRegistry(), kind);
        EXPECT_EQ(projection.facility, kind);
    }
    EXPECT_EQ(profileStateFingerprint(profile), fingerprint);
}

TEST(BaseFacilityManagementTest,
     WorkerWorldProjectionUsesExistingAggregateStaffingAndTaskFacts)
{
    ProfileState profile = makeNewAlphaProfile(
        "facility-worker-world", publishedContentRegistry());
    profile.worldClock.elapsedWorldMinutes = 1000U;

    EXPECT_FALSE(projectBaseFacilityWorkerWorldStatus(
        profile, publishedContentRegistry(), BaseFacilityKind::Storage)
                     .has_value());

    auto workshop = projectBaseFacilityWorkerWorldStatus(
        profile, publishedContentRegistry(), BaseFacilityKind::Workshop);
    ASSERT_TRUE(workshop.has_value());
    EXPECT_EQ(
        workshop->workSocket,
        BaseFacilityWorkSocketKind::WorkshopBench);
    EXPECT_EQ(workshop->status, BaseFacilityWorkerWorldStatus::Idle);
    EXPECT_EQ(
        workshop->profession,
        BaseResidentProfession::Engineering);

    profile.baseManufacturing.activeOrder = BaseManufacturingOrder{
        BaseServiceJobId{72U},
        BaseManufacturingRecipeDefinitionId{
            "base_manufacturing.weapon_maintenance_kit"},
        1U,
        BaseResidentProfession::Engineering,
        900U,
        1180U,
        {},
        AssetInstanceId{994U},
        false};
    workshop = projectBaseFacilityWorkerWorldStatus(
        profile, publishedContentRegistry(), BaseFacilityKind::Workshop);
    ASSERT_TRUE(workshop.has_value());
    EXPECT_EQ(workshop->status, BaseFacilityWorkerWorldStatus::Working);
    EXPECT_EQ(workshop->task, BaseFacilityTaskKind::Manufacturing);
    EXPECT_EQ(workshop->remainingMinutes, 180U);

    profile.baseManufacturing.activeOrder->outputReady = true;
    workshop = projectBaseFacilityWorkerWorldStatus(
        profile, publishedContentRegistry(), BaseFacilityKind::Workshop);
    ASSERT_TRUE(workshop.has_value());
    EXPECT_EQ(workshop->status, BaseFacilityWorkerWorldStatus::Idle);
    EXPECT_EQ(workshop->task, BaseFacilityTaskKind::OutputReady);

    profile.baseConstruction.facilities.at(
        BaseFacilityDefinitionId{"base_facility.workshop"}) =
        BaseConstructionState::FacilityPlacement::Reserve;
    workshop = projectBaseFacilityWorkerWorldStatus(
        profile, publishedContentRegistry(), BaseFacilityKind::Workshop);
    ASSERT_TRUE(workshop.has_value());
    EXPECT_EQ(workshop->status, BaseFacilityWorkerWorldStatus::Paused);

    profile.baseWorkforce.workshopWorker.reset();
    workshop = projectBaseFacilityWorkerWorldStatus(
        profile, publishedContentRegistry(), BaseFacilityKind::Workshop);
    ASSERT_TRUE(workshop.has_value());
    EXPECT_EQ(workshop->status, BaseFacilityWorkerWorldStatus::Missing);
    EXPECT_FALSE(workshop->profession.has_value());

    const std::uint64_t fingerprint = profileStateFingerprint(profile);
    static_cast<void>(projectBaseFacilityWorkerWorldStatus(
        profile, publishedContentRegistry(), BaseFacilityKind::Workshop));
    EXPECT_EQ(profileStateFingerprint(profile), fingerprint);
}

TEST(BaseFacilityManagementTest,
     MedicalWorkerWorldProjectionUsesTreatmentAndMissingStates)
{
    ProfileState profile = makeNewAlphaProfile(
        "facility-medical-worker-world", publishedContentRegistry());
    profile.worldClock.elapsedWorldMinutes = 1000U;
    profile.residentMedical.activeTreatment = ActiveResidentTreatment{
        BaseServiceJobId{73U},
        950U,
        1120U,
        2U,
        BaseResidentProfession::General,
        BaseResidentProfession::Medical};

    auto medical = projectBaseFacilityWorkerWorldStatus(
        profile, publishedContentRegistry(), BaseFacilityKind::Medical);
    ASSERT_TRUE(medical.has_value());
    EXPECT_EQ(
        medical->workSocket,
        BaseFacilityWorkSocketKind::MedicalBed);
    EXPECT_EQ(medical->status, BaseFacilityWorkerWorldStatus::Working);
    EXPECT_EQ(medical->profession, BaseResidentProfession::Medical);
    EXPECT_EQ(medical->task, BaseFacilityTaskKind::ResidentTreatment);
    EXPECT_EQ(medical->remainingMinutes, 120U);

    profile.baseWorkforce.medicalWorker.reset();
    medical = projectBaseFacilityWorkerWorldStatus(
        profile, publishedContentRegistry(), BaseFacilityKind::Medical);
    ASSERT_TRUE(medical.has_value());
    EXPECT_EQ(medical->status, BaseFacilityWorkerWorldStatus::Missing);
}

TEST(BaseFacilityManagementTest,
     ResidentWorldProjectionUsesAggregatePopulationAndWorkforceFacts)
{
    ProfileState profile = makeNewAlphaProfile(
        "facility-resident-world", publishedContentRegistry());
    const std::uint64_t fingerprint = profileStateFingerprint(profile);

    EXPECT_FALSE(projectBaseResidentWorldStatus(
        profile, BaseFacilityKind::Storage).has_value());

    auto residents = projectBaseResidentWorldStatus(
        profile, BaseFacilityKind::Dormitory);
    ASSERT_TRUE(residents.has_value());
    EXPECT_EQ(
        residents->workSocket,
        BaseFacilityWorkSocketKind::DormitoryBunk);
    EXPECT_EQ(residents->status, BaseResidentWorldStatus::Stable);
    EXPECT_EQ(residents->residents, 8U);
    EXPECT_EQ(residents->healthyResidents, 8U);
    EXPECT_EQ(residents->injuredResidents, 0U);
    EXPECT_EQ(residents->bedCapacity, 10U);
    EXPECT_EQ(residents->bedShortfall, 0U);
    EXPECT_EQ(residents->availableResidents, 6U);
    EXPECT_EQ(residents->assignedResidents, 2U);
    EXPECT_EQ(residents->constructionResidents, 0U);
    EXPECT_EQ(profileStateFingerprint(profile), fingerprint);

    profile.basePopulation.injuredResidents = 2U;
    profile.basePopulation.injuredByProfession = {2U, 0U, 0U, 0U};
    residents = projectBaseResidentWorldStatus(
        profile, BaseFacilityKind::Dormitory);
    ASSERT_TRUE(residents.has_value());
    EXPECT_EQ(residents->status, BaseResidentWorldStatus::Injured);
    EXPECT_EQ(residents->healthyResidents, 6U);
    EXPECT_EQ(residents->injuredResidents, 2U);

    profile.basePopulation.bedCapacity = 6U;
    residents = projectBaseResidentWorldStatus(
        profile, BaseFacilityKind::Dormitory);
    ASSERT_TRUE(residents.has_value());
    EXPECT_EQ(residents->status, BaseResidentWorldStatus::Overcrowded);
    EXPECT_EQ(residents->bedShortfall, 2U);
}

TEST(BaseFacilityManagementTest,
     ResidentWorldProjectionSeparatesConstructionAndEmptyPopulation)
{
    ProfileState profile = makeNewAlphaProfile(
        "facility-resident-construction", publishedContentRegistry());
    profile.baseConstruction.activeProject = ActiveBaseConstructionProject{
        BaseConstructionProjectDefinitionId{
            "base_construction.dormitory.level_2"},
        3U,
        2U,
        0U,
        480U};

    auto residents = projectBaseResidentWorldStatus(
        profile, BaseFacilityKind::Dormitory);
    ASSERT_TRUE(residents.has_value());
    EXPECT_EQ(residents->constructionResidents, 2U);
    EXPECT_EQ(residents->availableResidents, 4U);
    EXPECT_EQ(residents->assignedResidents, 2U);

    profile.basePopulation = BasePopulationState{0U, 0U};
    profile.baseWorkforce.workshopWorker.reset();
    profile.baseWorkforce.medicalWorker.reset();
    profile.baseConstruction.activeProject.reset();
    residents = projectBaseResidentWorldStatus(
        profile, BaseFacilityKind::Dormitory);
    ASSERT_TRUE(residents.has_value());
    EXPECT_EQ(residents->status, BaseResidentWorldStatus::Empty);
    EXPECT_EQ(residents->residents, 0U);
    EXPECT_EQ(residents->constructionResidents, 0U);
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
