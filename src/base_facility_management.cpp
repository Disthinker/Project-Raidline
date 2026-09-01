#include "base_facility_management.h"

#include <algorithm>

#include "base_construction_domain.h"
#include "base_manufacturing_domain.h"
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
    case BaseFacilityKind::Workshop:
        return BaseFacilityDefinitionId{"base_facility.workshop"};
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
}

BaseFacilityManagementProjection projectBaseFacilityManagement(
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

    const bool constructionActive = activeConstructionTargets(
        profile, content, kind);
    if (constructionActive)
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

    appendOpenAction(projection);
    if (projection.status != BaseFacilityOperationalStatus::Operational)
        return projection;

    appendStaffingAction(projection, profile);
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
        const BaseWorkforcePlan plan = queryAutoFillBaseWorkers(profile);
        projection.quickActions.push_back(
            BaseFacilityQuickActionProjection{
                BaseFacilityQuickActionKind::AutoFillWorkers,
                plan.canCommit,
                plan.message,
                std::nullopt});
    }
    return projection;
}
