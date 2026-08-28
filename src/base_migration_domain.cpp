#include "base_migration_domain.h"

#include "base_morale_domain.h"
#include "base_resource_domain.h"
#include "recovery_task_domain.h"

#include <algorithm>
#include <limits>

namespace
{
BaseMigrationPlan failure(
    const ProfileState &profile,
    DomainErrorCode error,
    std::string message,
    RegionalBaseSiteDefinitionId target = {})
{
    return {false, error, std::move(message), profile.revision, {},
            std::move(target)};
}

const RegionalBaseSiteDefinition *activeBaseSite(
    const ProfileState &profile,
    const ContentRegistry &content) noexcept
{
    const auto &sites = content.regionalOperations().baseSites;
    const auto found = std::find_if(
        sites.begin(), sites.end(),
        [&](const RegionalBaseSiteDefinition &site)
        { return site.nodeId == profile.regionalOperations.activeBaseNodeId; });
    return found == sites.end() ? nullptr : &*found;
}

}

BaseMigrationPlan queryBaseMigration(
    const ProfileState &profile,
    const ContentRegistry &content,
    const BaseMigrationCommand &command) noexcept
{
    try
    {
        const RegionalBaseSiteDefinition &target =
            content.regionalBaseSite(command.targetSiteDefinitionId);
        const RegionalBaseSiteDefinition *source = activeBaseSite(
            profile, content);
        if (source == nullptr)
        {
            return failure(
                profile, DomainErrorCode::InvalidProfile,
                "active main Base site is unknown",
                command.targetSiteDefinitionId);
        }
        if (profile.pendingRaid.has_value())
        {
            return failure(
                profile, DomainErrorCode::IllegalDestination,
                "main Base migration is unavailable during a Raid",
                command.targetSiteDefinitionId);
        }
        if (profile.baseSiege.warningActive)
        {
            return failure(
                profile, DomainErrorCode::IllegalDestination,
                "main Base migration is locked during siege warning",
                command.targetSiteDefinitionId);
        }
        if (target.id == source->id)
        {
            return failure(
                profile, DomainErrorCode::IllegalDestination,
                "target site is already the active main Base",
                command.targetSiteDefinitionId);
        }
        const auto targetState = profile.regionalOperations.baseSites.find(
            target.id);
        if (targetState == profile.regionalOperations.baseSites.end() ||
            !targetState->second.unlocked)
        {
            return failure(
                profile, DomainErrorCode::IllegalDestination,
                "target Base site is locked",
                command.targetSiteDefinitionId);
        }
        if (!target.outpostDefinitionId.has_value())
        {
            return failure(
                profile, DomainErrorCode::InvalidProfile,
                "target Base site has no migration staging outpost",
                command.targetSiteDefinitionId);
        }
        const auto targetOutpost = profile.regionalOperations.outposts.find(
            *target.outpostDefinitionId);
        if (targetOutpost == profile.regionalOperations.outposts.end() ||
            !targetOutpost->second.established)
        {
            return failure(
                profile, DomainErrorCode::IllegalDestination,
                "target Base site outpost must be established first",
                command.targetSiteDefinitionId);
        }
        if (profile.regionalOperations.technologyCore.instanceId !=
                "technology_core.primary" ||
            profile.regionalOperations.technologyCore.baseSiteDefinitionId !=
                source->id)
        {
            return failure(
                profile, DomainErrorCode::InvalidProfile,
                "technology core is not at the active main Base",
                command.targetSiteDefinitionId);
        }

        BaseMigrationPlan plan{
            true,
            DomainErrorCode::None,
            {},
            profile.revision,
            source->id,
            target.id,
            target.migrationMinutes};
        for (const BaseFacilityDefinition &facility : content.baseFacilities())
        {
            if (facility.requiredForMigration &&
                !baseFacilityOwned(profile, facility.id))
            {
                plan.missingRequiredFacilities.push_back(facility.id);
            }
            if (!facility.requiredForMigration &&
                baseFacilityOwned(profile, facility.id))
            {
                plan.facilitiesEnteringReserve.push_back(facility.id);
            }
        }
        if (!plan.missingRequiredFacilities.empty())
        {
            plan.canCommit = false;
            plan.error = DomainErrorCode::Capacity;
            plan.message = "required migration facilities are missing";
            return plan;
        }
        const std::size_t required = static_cast<std::size_t>(std::count_if(
            content.baseFacilities().begin(), content.baseFacilities().end(),
            [](const BaseFacilityDefinition &facility)
            { return facility.requiredForMigration; }));
        if (target.coreFacilitySlots < required)
        {
            plan.canCommit = false;
            plan.error = DomainErrorCode::Capacity;
            plan.message = "target site cannot place the required facilities";
            return plan;
        }
        WorldClockState arrival = profile.worldClock;
        const WorldClockAdvanceResult advanced = advanceWorldClock(
            arrival, target.migrationMinutes);
        if (advanced.minutesApplied != target.migrationMinutes)
        {
            plan.canCommit = false;
            plan.error = DomainErrorCode::Capacity;
            plan.message = "migration world time cannot advance";
            return plan;
        }
        plan.arrival = projectWorldClock(arrival);
        bool deadlinesValid = true;
        for (const auto &[definitionId, placement] :
             profile.baseConstruction.facilities)
        {
            std::uint64_t pausedMinutes = target.migrationMinutes;
            if (placement ==
                BaseConstructionState::FacilityPlacement::Reserve)
            {
                const std::optional<std::uint64_t> reserveStarted =
                    baseFacilityReserveStartedWorldMinute(
                        profile, definitionId);
                if (!reserveStarted.has_value() ||
                    *reserveStarted >
                        profile.worldClock.elapsedWorldMinutes ||
                    profile.worldClock.elapsedWorldMinutes -
                            *reserveStarted >
                        std::numeric_limits<std::uint64_t>::max() -
                            pausedMinutes)
                {
                    deadlinesValid = false;
                    break;
                }
                pausedMinutes += profile.worldClock.elapsedWorldMinutes -
                    *reserveStarted;
            }
            deadlinesValid = deadlinesValid && canShiftBaseFacilityTasks(
                profile, content, definitionId, pausedMinutes);
        }
        if (!deadlinesValid ||
            profile.revision == std::numeric_limits<ProfileRevision>::max())
        {
            plan.canCommit = false;
            plan.error = DomainErrorCode::RevisionOverflow;
            plan.message = "migration state cannot advance safely";
        }
        return plan;
    }
    catch (const std::exception &error)
    {
        return failure(
            profile, DomainErrorCode::IllegalDestination, error.what(),
            command.targetSiteDefinitionId);
    }
}

BaseMigrationReceipt executeBaseMigration(
    ProfileState &profile,
    const ContentRegistry &content,
    const BaseMigrationCommand &command,
    const CommandContext &context)
{
    if (context.transactionId.empty())
    {
        return {false, false, DomainErrorCode::InvalidTransaction,
                "transaction ID is empty", profile.revision};
    }
    if (profile.committedTransactions.contains(context.transactionId))
    {
        return {true, true, DomainErrorCode::None, {}, profile.revision};
    }
    if (context.expectedRevision != profile.revision)
    {
        return {false, false, DomainErrorCode::StaleRevision,
                "profile revision is stale", profile.revision};
    }
    const BaseMigrationPlan plan = queryBaseMigration(
        profile, content, command);
    if (!plan.canCommit)
    {
        return {false, false, plan.error, plan.message, profile.revision,
                plan.sourceSiteDefinitionId,
                plan.targetSiteDefinitionId};
    }

    ProfileState candidate = profile;
    for (const auto &[definitionId, placement] :
         candidate.baseConstruction.facilities)
    {
        std::uint64_t pausedMinutes = plan.migrationMinutes;
        if (placement == BaseConstructionState::FacilityPlacement::Reserve)
        {
            pausedMinutes += candidate.worldClock.elapsedWorldMinutes -
                candidate.baseConstruction.facilityReserveStartedWorldMinutes
                    .at(definitionId);
        }
        shiftBaseFacilityTasks(
            candidate, content, definitionId, pausedMinutes);
    }
    const WorldClockAdvanceResult advanced = advanceWorldClock(
        candidate.worldClock, plan.migrationMinutes);
    if (advanced.minutesApplied != plan.migrationMinutes)
    {
        return {false, false, DomainErrorCode::Capacity,
                "migration world time could not be committed",
                profile.revision};
    }
    static_cast<void>(synchronizeBaseDailySystemsThrough(candidate, content));
    static_cast<void>(applyRecoveryTaskThrough(candidate));

    for (auto &[definitionId, placement] :
         candidate.baseConstruction.facilities)
    {
        const BaseFacilityDefinition &facility =
            content.baseFacility(definitionId);
        placement = facility.requiredForMigration
            ? BaseConstructionState::FacilityPlacement::Installed
            : BaseConstructionState::FacilityPlacement::Reserve;
        if (facility.requiredForMigration)
        {
            candidate.baseConstruction.facilityReserveStartedWorldMinutes
                .erase(definitionId);
        }
        else
        {
            candidate.baseConstruction.facilityReserveStartedWorldMinutes[
                definitionId] =
                candidate.worldClock.elapsedWorldMinutes;
        }
    }

    const RegionalBaseSiteDefinition &source =
        content.regionalBaseSite(plan.sourceSiteDefinitionId);
    const RegionalBaseSiteDefinition &target =
        content.regionalBaseSite(plan.targetSiteDefinitionId);
    if (target.outpostDefinitionId.has_value())
    {
        RegionalOutpostState &targetOutpost =
            candidate.regionalOperations.outposts.at(
                *target.outpostDefinitionId);
        targetOutpost.established = false;
        targetOutpost.disrupted = false;
        targetOutpost.assignedStaff = {};
        targetOutpost.shortcutOperationsSinceRestoration = 0U;
    }
    if (source.outpostDefinitionId.has_value())
    {
        RegionalOutpostState &sourceOutpost =
            candidate.regionalOperations.outposts.at(
                *source.outpostDefinitionId);
        sourceOutpost.unlocked = true;
        sourceOutpost.established = true;
        sourceOutpost.disrupted = false;
        sourceOutpost.assignedStaff = {};
        sourceOutpost.shortcutOperationsSinceRestoration = 0U;
    }
    candidate.regionalOperations.activeBaseNodeId = target.nodeId;
    candidate.regionalOperations.technologyCore.baseSiteDefinitionId =
        target.id;
    candidate.committedTransactions.insert(context.transactionId);
    ++candidate.revision;
    const ProfileValidationResult validation = validateProfileState(
        candidate, content);
    if (!validation.valid)
    {
        return {false, false, DomainErrorCode::InvalidProfile,
                validation.message, profile.revision};
    }
    profile = std::move(candidate);
    return {true,
            false,
            DomainErrorCode::None,
            {},
            profile.revision,
            plan.sourceSiteDefinitionId,
            plan.targetSiteDefinitionId,
            plan.migrationMinutes,
            projectWorldClock(profile.worldClock),
            plan.facilitiesEnteringReserve};
}
