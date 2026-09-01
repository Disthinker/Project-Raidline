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
    BaseFacilityKind kind) noexcept
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
        return projection;
    }

    if (kind == BaseFacilityKind::Workshop)
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

