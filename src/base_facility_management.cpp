#include "base_facility_management.h"

#include <algorithm>

#include "base_construction_domain.h"
#include "base_manufacturing_domain.h"
#include "base_population_domain.h"
#include "base_resident_medical_domain.h"
#include "base_workforce_domain.h"

namespace
{
std::optional<BaseFacilityDefinitionId> definitionId(
    BaseFacilityKind kind)
{
    switch (kind)
    {
    case BaseFacilityKind::Storage:
        return BaseFacilityDefinitionId{"base_facility.warehouse"};
    case BaseFacilityKind::Medical:
        return BaseFacilityDefinitionId{"base_facility.medical"};
    case BaseFacilityKind::Dormitory:
        return BaseFacilityDefinitionId{"base_facility.dormitory"};
    case BaseFacilityKind::KitchenWater:
        return BaseFacilityDefinitionId{"base_facility.kitchen_water"};
    case BaseFacilityKind::Workshop:
        return BaseFacilityDefinitionId{"base_facility.workshop"};
    case BaseFacilityKind::Supply:
    case BaseFacilityKind::Allocation:
    case BaseFacilityKind::RaidGate:
        return std::nullopt;
    }
    return std::nullopt;
}

std::optional<BaseFacilityWorkSocketKind> workSocketKind(
    BaseFacilityKind kind) noexcept
{
    switch (kind)
    {
    case BaseFacilityKind::Storage:
        return BaseFacilityWorkSocketKind::StorageHandling;
    case BaseFacilityKind::Medical:
        return BaseFacilityWorkSocketKind::MedicalBed;
    case BaseFacilityKind::Dormitory:
        return BaseFacilityWorkSocketKind::DormitoryBunk;
    case BaseFacilityKind::KitchenWater:
        return BaseFacilityWorkSocketKind::KitchenProcessing;
    case BaseFacilityKind::Workshop:
        return BaseFacilityWorkSocketKind::WorkshopBench;
    case BaseFacilityKind::Supply:
    case BaseFacilityKind::Allocation:
    case BaseFacilityKind::RaidGate:
        return std::nullopt;
    }
    return std::nullopt;
}

std::optional<BaseFacilityUpgradeTarget> upgradeTarget(
    BaseFacilityKind kind) noexcept
{
    switch (kind)
    {
    case BaseFacilityKind::Dormitory:
        return BaseFacilityUpgradeTarget::Dormitory;
    case BaseFacilityKind::KitchenWater:
        return BaseFacilityUpgradeTarget::KitchenWater;
    case BaseFacilityKind::Medical:
        return BaseFacilityUpgradeTarget::Medical;
    case BaseFacilityKind::Workshop:
        return BaseFacilityUpgradeTarget::Workshop;
    case BaseFacilityKind::Storage:
    case BaseFacilityKind::Supply:
    case BaseFacilityKind::Allocation:
    case BaseFacilityKind::RaidGate:
        return std::nullopt;
    }
    return std::nullopt;
}

std::optional<BaseFacilityStaffingKind> staffingKind(
    BaseFacilityKind kind) noexcept
{
    switch (kind)
    {
    case BaseFacilityKind::Workshop:
        return BaseFacilityStaffingKind::Workshop;
    case BaseFacilityKind::Medical:
        return BaseFacilityStaffingKind::Medical;
    case BaseFacilityKind::Storage:
    case BaseFacilityKind::Supply:
    case BaseFacilityKind::Allocation:
    case BaseFacilityKind::Dormitory:
    case BaseFacilityKind::KitchenWater:
    case BaseFacilityKind::RaidGate:
        return std::nullopt;
    }
    return std::nullopt;
}

const BaseConstructionProjectDefinition *upgradeProject(
    const ContentRegistry &content,
    BaseFacilityKind kind) noexcept
{
    const auto target = upgradeTarget(kind);
    if (!target.has_value())
        return nullptr;
    const auto &projects = content.baseConstructionProjects();
    const auto found = std::find_if(
        projects.begin(), projects.end(),
        [&](const BaseConstructionProjectDefinition &candidate)
        {
            return candidate.target == *target;
        });
    return found == projects.end() ? nullptr : &*found;
}

void appendOpenAction(BaseFacilityManagementProjection &projection)
{
    const bool operational = projection.status ==
        BaseFacilityOperationalStatus::Operational;
    projection.quickActions.push_back(BaseFacilityQuickActionProjection{
        BaseFacilityQuickActionKind::OpenFunction,
        operational,
        operational
            ? std::string{"facility function is available"}
            : std::string{"facility must be installed before use"},
        std::nullopt});
}

void appendStaffingAction(
    BaseFacilityManagementProjection &projection,
    const ProfileState &profile)
{
    const auto facility = staffingKind(projection.kind);
    if (!facility.has_value())
        return;
    const BaseFacilityStaffingCommand command{*facility};
    if (projection.assignedWorker.has_value())
    {
        const BaseWorkforcePlan plan = queryClearBaseWorker(profile, command);
        projection.quickActions.push_back(BaseFacilityQuickActionProjection{
            BaseFacilityQuickActionKind::ClearWorker,
            plan.canCommit,
            plan.message,
            std::nullopt});
        return;
    }
    const BaseWorkforcePlan plan = queryAssignBestBaseWorker(profile, command);
    projection.quickActions.push_back(BaseFacilityQuickActionProjection{
        BaseFacilityQuickActionKind::AssignBestWorker,
        plan.canCommit,
        plan.message,
        std::nullopt});
}

void appendAutoFillStaffingAction(
    BaseFacilityManagementProjection &projection,
    const ProfileState &profile)
{
    const BaseWorkforcePlan plan = queryAutoFillBaseWorkers(profile);
    projection.quickActions.push_back(BaseFacilityQuickActionProjection{
        BaseFacilityQuickActionKind::AutoFillWorkers,
        plan.canCommit,
        plan.message,
        std::nullopt});
}

void appendUpgradeAction(
    BaseFacilityManagementProjection &projection,
    const ProfileState &profile,
    const ContentRegistry &content)
{
    const BaseConstructionProjectDefinition *project =
        upgradeProject(content, projection.kind);
    if (project == nullptr)
        return;
    const auto &active = profile.baseConstruction.activeProject;
    const bool matchingActive = active.has_value() &&
        active->definitionId == project->id;
    if (matchingActive)
    {
        const BaseConstructionPlan plan = queryCancelBaseConstruction(
            profile,
            content,
            CancelBaseConstructionCommand{project->id});
        projection.quickActions.push_back(BaseFacilityQuickActionProjection{
            BaseFacilityQuickActionKind::CancelUpgrade,
            plan.canCommit,
            plan.message,
            project->id});
        return;
    }
    const auto target = upgradeTarget(projection.kind);
    if (!target.has_value() ||
        baseFacilityLevel(profile.baseConstruction, *target) >=
            project->targetLevel)
    {
        return;
    }
    const BaseConstructionPlan plan = queryStartBaseConstruction(
        profile,
        content,
        StartBaseConstructionCommand{project->id});
    projection.quickActions.push_back(BaseFacilityQuickActionProjection{
        BaseFacilityQuickActionKind::StartUpgrade,
        plan.canCommit,
        plan.message,
        project->id});
}

bool activeConstructionTargets(
    const ProfileState &profile,
    const ContentRegistry &content,
    BaseFacilityKind kind) noexcept
{
    if (!profile.baseConstruction.activeProject.has_value())
        return false;
    const auto target = upgradeTarget(kind);
    if (!target.has_value())
        return false;
    const auto &projects = content.baseConstructionProjects();
    const auto project = std::find_if(
        projects.begin(), projects.end(),
        [&](const BaseConstructionProjectDefinition &candidate)
        {
            return candidate.id ==
                profile.baseConstruction.activeProject->definitionId;
        });
    return project != projects.end() && project->target == *target;
}

std::optional<BaseFacilityKind> facilityKind(
    BaseFacilityUpgradeTarget target) noexcept
{
    switch (target)
    {
    case BaseFacilityUpgradeTarget::Dormitory:
        return BaseFacilityKind::Dormitory;
    case BaseFacilityUpgradeTarget::KitchenWater:
        return BaseFacilityKind::KitchenWater;
    case BaseFacilityUpgradeTarget::Workshop:
        return BaseFacilityKind::Workshop;
    case BaseFacilityUpgradeTarget::Medical:
        return BaseFacilityKind::Medical;
    }
    return std::nullopt;
}

bool facilityOperational(
    const ProfileState &profile,
    BaseFacilityKind kind) noexcept
{
    const auto id = definitionId(kind);
    return !id.has_value() || baseFacilityInstalled(profile, *id);
}

bool facilityWorkPaused(
    const ProfileState &profile,
    BaseFacilityKind kind) noexcept
{
    const auto id = definitionId(kind);
    return id.has_value() && baseFacilityOwned(profile, *id) &&
        !baseFacilityInstalled(profile, *id);
}

BaseFacilityManagementProjection projectManagementFacts(
    const ProfileState &profile,
    const ContentRegistry &content,
    BaseFacilityKind kind)
{
    BaseFacilityManagementProjection projection;
    projection.kind = kind;

    if (const auto id = definitionId(kind); id.has_value())
    {
        if (baseFacilityInstalled(profile, *id))
            projection.status = BaseFacilityOperationalStatus::Operational;
        else if (baseFacilityOwned(profile, *id))
            projection.status = BaseFacilityOperationalStatus::Reserve;
        else
            projection.status = BaseFacilityOperationalStatus::Unavailable;
    }

    if (const auto target = upgradeTarget(kind); target.has_value())
        projection.level = baseFacilityLevel(
            profile.baseConstruction, *target);

    const BaseWorkforceProjection workforce = projectBaseWorkforce(profile);
    if (kind == BaseFacilityKind::Workshop)
    {
        projection.staffingApplicable = true;
        projection.assignedWorker = workforce.workshopWorker;
    }
    else if (kind == BaseFacilityKind::Medical)
    {
        projection.staffingApplicable = true;
        projection.assignedWorker = workforce.medicalWorker;
    }

    if (activeConstructionTargets(profile, content, kind))
    {
        const BaseConstructionProjection construction =
            projectBaseConstruction(profile, content);
        projection.task = BaseFacilityTaskKind::Construction;
        projection.remainingMinutes = construction.remainingMinutes;
    }
    else if (kind == BaseFacilityKind::Workshop)
    {
        const BaseManufacturingProjection manufacturing =
            projectBaseManufacturing(profile);
        if (manufacturing.orderPresent)
        {
            projection.task = manufacturing.outputReady
                ? BaseFacilityTaskKind::OutputReady
                : BaseFacilityTaskKind::Manufacturing;
            projection.remainingMinutes = manufacturing.remainingMinutes;
        }
    }
    else if (kind == BaseFacilityKind::Medical)
    {
        const BaseResidentMedicalProjection medical =
            projectBaseResidentMedical(profile);
        if (medical.treatmentActive)
        {
            projection.task = BaseFacilityTaskKind::ResidentTreatment;
            projection.remainingMinutes = medical.remainingMinutes;
        }
    }
    return projection;
}
}

BaseFacilityManagementProjection projectBaseFacilityManagement(
    const ProfileState &profile,
    const ContentRegistry &content,
    BaseFacilityKind kind)
{
    BaseFacilityManagementProjection projection = projectManagementFacts(
        profile, content, kind);

    appendOpenAction(projection);
    if (projection.status != BaseFacilityOperationalStatus::Operational)
        return projection;

    appendStaffingAction(projection, profile);
    if (projection.staffingApplicable)
        appendAutoFillStaffingAction(projection, profile);
    appendUpgradeAction(projection, profile, content);

    if (kind == BaseFacilityKind::Workshop)
    {
        const BaseManufacturingProjection manufacturing =
            projectBaseManufacturing(profile);
        if (manufacturing.orderPresent && manufacturing.outputReady)
        {
            const BaseManufacturingReturnPlan plan =
                queryCollectBaseManufacturing(profile, content);
            projection.quickActions.push_back(
                BaseFacilityQuickActionProjection{
                    BaseFacilityQuickActionKind::CollectManufacturing,
                    plan.canCommit,
                    plan.message,
                    std::nullopt});
        }
        else if (manufacturing.orderPresent)
        {
            const BaseManufacturingReturnPlan plan =
                queryCancelBaseManufacturing(profile, content);
            projection.quickActions.push_back(
                BaseFacilityQuickActionProjection{
                    BaseFacilityQuickActionKind::CancelManufacturing,
                    plan.canCommit,
                    plan.message,
                    std::nullopt});
        }
    }
    else if (kind == BaseFacilityKind::Medical)
    {
        const ResidentTreatmentPlan plan = queryStartResidentTreatment(
            profile, content);
        projection.quickActions.push_back(
            BaseFacilityQuickActionProjection{
                BaseFacilityQuickActionKind::StartResidentTreatment,
                plan.canCommit,
                plan.message,
                std::nullopt});
    }
    else if (kind == BaseFacilityKind::Dormitory)
    {
        appendAutoFillStaffingAction(projection, profile);
    }
    return projection;
}

BaseFacilityWorldServiceProjection projectBaseFacilityWorldService(
    const ProfileState &profile,
    const ContentRegistry &content,
    BaseFacilityKind kind)
{
    const BaseFacilityManagementProjection management =
        projectManagementFacts(profile, content, kind);
    BaseFacilityWorldServiceProjection projection{
        kind,
        BaseFacilityWorldServiceStatus::Ready,
        management.task,
        management.remainingMinutes};

    if (management.status != BaseFacilityOperationalStatus::Operational)
    {
        projection.status = BaseFacilityWorldServiceStatus::Blocked;
    }
    else if (management.task == BaseFacilityTaskKind::OutputReady)
    {
        projection.status = BaseFacilityWorldServiceStatus::OutputReady;
    }
    else if (management.task == BaseFacilityTaskKind::Construction ||
             management.task == BaseFacilityTaskKind::Manufacturing ||
             management.task == BaseFacilityTaskKind::ResidentTreatment)
    {
        projection.status = BaseFacilityWorldServiceStatus::Working;
    }
    else if (management.staffingApplicable &&
             !management.assignedWorker.has_value())
    {
        projection.status = BaseFacilityWorldServiceStatus::NeedsStaff;
    }
    if (projection.status == BaseFacilityWorldServiceStatus::Working ||
        projection.status == BaseFacilityWorldServiceStatus::OutputReady)
    {
        projection.activeWorkSocket = workSocketKind(kind);
    }
    return projection;
}

std::optional<BaseFacilityWorkerWorldProjection>
projectBaseFacilityWorkerWorldStatus(
    const ProfileState &profile,
    const ContentRegistry &content,
    BaseFacilityKind kind)
{
    if (kind != BaseFacilityKind::Workshop &&
        kind != BaseFacilityKind::Medical)
    {
        return std::nullopt;
    }

    const BaseFacilityManagementProjection management =
        projectManagementFacts(profile, content, kind);
    const std::optional<BaseFacilityWorkSocketKind> socket =
        workSocketKind(kind);
    if (!socket.has_value())
        return std::nullopt;

    BaseFacilityWorkerWorldProjection projection{
        kind,
        *socket,
        BaseFacilityWorkerWorldStatus::Idle,
        management.assignedWorker,
        management.task,
        management.remainingMinutes};

    if (!management.assignedWorker.has_value())
    {
        projection.status = BaseFacilityWorkerWorldStatus::Missing;
    }
    else if (management.status != BaseFacilityOperationalStatus::Operational)
    {
        projection.status = BaseFacilityWorkerWorldStatus::Paused;
    }
    else if (management.task == BaseFacilityTaskKind::Construction ||
             management.task == BaseFacilityTaskKind::Manufacturing ||
             management.task == BaseFacilityTaskKind::ResidentTreatment)
    {
        projection.status = BaseFacilityWorkerWorldStatus::Working;
    }
    return projection;
}

std::optional<BaseResidentWorldProjection>
projectBaseResidentWorldStatus(
    const ProfileState &profile,
    BaseFacilityKind kind) noexcept
{
    if (kind != BaseFacilityKind::Dormitory)
        return std::nullopt;

    const BasePopulationProjection population = projectBasePopulation(
        profile.basePopulation);
    const BaseWorkforceProjection workforce = projectBaseWorkforce(profile);
    const std::uint64_t classified =
        static_cast<std::uint64_t>(workforce.availableResidents) +
        workforce.assignedResidents;
    const std::uint32_t constructionResidents =
        classified < population.healthyResidents
        ? population.healthyResidents -
              static_cast<std::uint32_t>(classified)
        : 0U;

    BaseResidentWorldStatus status{BaseResidentWorldStatus::Stable};
    if (population.ordinaryResidents == 0U)
        status = BaseResidentWorldStatus::Empty;
    else if (population.bedShortfall > 0U)
        status = BaseResidentWorldStatus::Overcrowded;
    else if (population.injuredResidents > 0U)
        status = BaseResidentWorldStatus::Injured;

    return BaseResidentWorldProjection{
        BaseFacilityKind::Dormitory,
        BaseFacilityWorkSocketKind::DormitoryBunk,
        status,
        population.ordinaryResidents,
        population.healthyResidents,
        population.injuredResidents,
        population.bedCapacity,
        population.bedShortfall,
        workforce.availableResidents,
        workforce.assignedResidents,
        constructionResidents};
}

BaseOperationsOverviewProjection projectBaseOperationsOverview(
    const ProfileState &profile,
    const ContentRegistry &content)
{
    BaseOperationsOverviewProjection result;

    const BaseManufacturingProjection manufacturing =
        projectBaseManufacturing(profile);
    if (manufacturing.orderPresent && manufacturing.outputReady)
    {
        result.entries.push_back(BaseOperationOverviewEntry{
            BaseFacilityKind::Workshop,
            BaseOperationOverviewKind::OutputReady,
            0U,
            !facilityOperational(profile, BaseFacilityKind::Workshop)});
    }

    const BaseConstructionProjection construction =
        projectBaseConstruction(profile, content);
    if (construction.activeProjectId.has_value())
    {
        const auto &projects = content.baseConstructionProjects();
        const auto project = std::find_if(
            projects.begin(), projects.end(),
            [&](const BaseConstructionProjectDefinition &candidate)
            {
                return candidate.id == *construction.activeProjectId;
            });
        if (project != projects.end())
        {
            if (const auto facility = facilityKind(project->target);
                facility.has_value())
            {
                result.entries.push_back(BaseOperationOverviewEntry{
                    *facility,
                    BaseOperationOverviewKind::Construction,
                    construction.remainingMinutes,
                    facilityWorkPaused(profile, *facility)});
            }
        }
    }

    const BaseResidentMedicalProjection medical =
        projectBaseResidentMedical(profile);
    if (medical.treatmentActive)
    {
        result.entries.push_back(BaseOperationOverviewEntry{
            BaseFacilityKind::Medical,
            BaseOperationOverviewKind::ResidentTreatment,
            medical.remainingMinutes,
            !facilityOperational(profile, BaseFacilityKind::Medical)});
    }

    if (manufacturing.orderPresent && !manufacturing.outputReady)
    {
        result.entries.push_back(BaseOperationOverviewEntry{
            BaseFacilityKind::Workshop,
            BaseOperationOverviewKind::Manufacturing,
            manufacturing.remainingMinutes,
            !facilityOperational(profile, BaseFacilityKind::Workshop)});
    }

    const BaseWorkforceProjection workforce = projectBaseWorkforce(profile);
    if (facilityOperational(profile, BaseFacilityKind::Workshop) &&
        !workforce.workshopWorker.has_value())
    {
        result.entries.push_back(BaseOperationOverviewEntry{
            BaseFacilityKind::Workshop,
            BaseOperationOverviewKind::StaffingGap});
    }
    if (facilityOperational(profile, BaseFacilityKind::Medical) &&
        !workforce.medicalWorker.has_value())
    {
        result.entries.push_back(BaseOperationOverviewEntry{
            BaseFacilityKind::Medical,
            BaseOperationOverviewKind::StaffingGap});
    }
    return result;
}
