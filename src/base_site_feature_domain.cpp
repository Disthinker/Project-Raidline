#include "base_site_feature_domain.h"

#include "base_manufacturing_domain.h"
#include "base_morale_domain.h"
#include "base_resident_medical_domain.h"
#include "base_resource_domain.h"
#include "base_workforce_domain.h"
#include "recovery_task_domain.h"

#include <algorithm>
#include <limits>

namespace
{
BaseSiteFeatureRepairPlan failure(
    const ProfileState &profile,
    DomainErrorCode error,
    std::string message,
    RegionalBaseSiteDefinitionId siteDefinitionId = {})
{
    return {false, error, std::move(message), profile.revision,
            std::move(siteDefinitionId)};
}

const RegionalBaseSiteDefinition *activeSite(
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

BaseSiteFeatureRepairPlan queryBaseSiteFeatureRepair(
    const ProfileState &profile,
    const ContentRegistry &content,
    const BaseSiteFeatureRepairCommand &command) noexcept
{
    try
    {
        const RegionalBaseSiteDefinition &site =
            content.regionalBaseSite(command.siteDefinitionId);
        const RegionalBaseSiteDefinition *active = activeSite(profile, content);
        if (active == nullptr)
        {
            return failure(
                profile, DomainErrorCode::InvalidProfile,
                "active main Base site is unknown",
                site.id);
        }
        if (profile.pendingRaid.has_value())
        {
            return failure(
                profile, DomainErrorCode::IllegalDestination,
                "Base site feature repair is unavailable during a Raid",
                site.id);
        }
        const auto state = profile.regionalOperations.baseSites.find(site.id);
        if (state == profile.regionalOperations.baseSites.end())
        {
            return failure(
                profile, DomainErrorCode::InvalidProfile,
                "Base site feature state is missing", site.id);
        }
        if (!state->second.unlocked)
        {
            return failure(
                profile, DomainErrorCode::IllegalDestination,
                "Base site must be secured before repairing its feature",
                site.id);
        }
        if (active->id != site.id)
        {
            if (!site.outpostDefinitionId.has_value())
            {
                return failure(
                    profile, DomainErrorCode::IllegalDestination,
                    "remote Base site has no staging outpost", site.id);
            }
            const auto outpost = profile.regionalOperations.outposts.find(
                *site.outpostDefinitionId);
            if (outpost == profile.regionalOperations.outposts.end() ||
                !outpost->second.established)
            {
                return failure(
                    profile, DomainErrorCode::IllegalDestination,
                    "establish the staging outpost before repairing the site feature",
                    site.id);
            }
        }
        if (state->second.uniqueFeatureRepaired)
        {
            return failure(
                profile, DomainErrorCode::IllegalDestination,
                "Base site feature is already repaired", site.id);
        }
        if (!baseFacilityInstalled(
                profile,
                BaseFacilityDefinitionId{"base_facility.workshop"}))
        {
            return failure(
                profile, DomainErrorCode::IllegalDestination,
                "workshop must be installed before repairing the site feature",
                site.id);
        }
        if (profile.baseConstruction.materialUnits <
            site.uniqueFeatureRepairMaterialUnits)
        {
            return failure(
                profile, DomainErrorCode::Capacity,
                "insufficient Base construction material", site.id);
        }
        if (availableBaseWorkers(profile) < 1U)
        {
            return failure(
                profile, DomainErrorCode::Capacity,
                "insufficient available Base workers", site.id);
        }
        if (profile.revision == std::numeric_limits<ProfileRevision>::max() ||
            profile.worldClock.elapsedWorldMinutes >
                std::numeric_limits<std::uint64_t>::max() -
                    site.uniqueFeatureRepairMinutes)
        {
            return failure(
                profile, DomainErrorCode::RevisionOverflow,
                "Base site feature repair cannot advance safely", site.id);
        }
        WorldClockState arrival = profile.worldClock;
        const WorldClockAdvanceResult advanced = advanceWorldClock(
            arrival, site.uniqueFeatureRepairMinutes);
        if (advanced.minutesApplied != site.uniqueFeatureRepairMinutes)
        {
            return failure(
                profile, DomainErrorCode::Capacity,
                "Base site feature repair time cannot advance", site.id);
        }
        return {
            true,
            DomainErrorCode::None,
            {},
            profile.revision,
            site.id,
            site.uniqueFeatureRepairMaterialUnits,
            1U,
            site.uniqueFeatureRepairMinutes,
            site.uniqueFeatureManufacturingDurationPercent,
            projectWorldClock(arrival)};
    }
    catch (const std::exception &error)
    {
        return failure(
            profile, DomainErrorCode::IllegalDestination, error.what(),
            command.siteDefinitionId);
    }
}

BaseSiteFeatureRepairReceipt executeBaseSiteFeatureRepair(
    ProfileState &profile,
    const ContentRegistry &content,
    const BaseSiteFeatureRepairCommand &command,
    const CommandContext &context)
{
    if (context.transactionId.empty())
    {
        return {false, false, DomainErrorCode::InvalidTransaction,
                "transaction ID is empty", profile.revision};
    }
    if (profile.committedTransactions.contains(context.transactionId))
    {
        return {true, true, DomainErrorCode::None, {}, profile.revision,
                command.siteDefinitionId};
    }
    if (context.expectedRevision != profile.revision)
    {
        return {false, false, DomainErrorCode::StaleRevision,
                "profile revision is stale", profile.revision,
                command.siteDefinitionId};
    }
    const BaseSiteFeatureRepairPlan plan = queryBaseSiteFeatureRepair(
        profile, content, command);
    if (!plan.canCommit)
    {
        return {false, false, plan.error, plan.message, profile.revision,
                plan.siteDefinitionId};
    }

    ProfileState candidate = profile;
    candidate.baseConstruction.materialUnits -= plan.materialUnits;
    const WorldClockAdvanceResult advanced = advanceWorldClock(
        candidate.worldClock, plan.durationMinutes);
    if (advanced.minutesApplied != plan.durationMinutes)
    {
        return {false, false, DomainErrorCode::Capacity,
                "Base site feature repair time could not be committed",
                profile.revision, plan.siteDefinitionId};
    }
    static_cast<void>(synchronizeBaseDailySystemsThrough(candidate, content));
    static_cast<void>(applyBaseConstructionThrough(candidate, content));
    static_cast<void>(applyBaseManufacturingThrough(candidate, content));
    static_cast<void>(applyResidentTreatmentThrough(candidate));
    static_cast<void>(applyRecoveryTaskThrough(candidate));
    candidate.regionalOperations.baseSites.at(plan.siteDefinitionId)
        .uniqueFeatureRepaired = true;
    candidate.committedTransactions.insert(context.transactionId);
    ++candidate.revision;
    const ProfileValidationResult validation = validateProfileState(
        candidate, content);
    if (!validation.valid)
    {
        return {false, false, DomainErrorCode::InvalidProfile,
                validation.message, profile.revision,
                plan.siteDefinitionId};
    }
    profile = std::move(candidate);
    return {
        true,
        false,
        DomainErrorCode::None,
        {},
        profile.revision,
        plan.siteDefinitionId,
        plan.materialUnits,
        advanced.minutesApplied,
        plan.manufacturingDurationPercent};
}

std::uint32_t activeBaseSiteManufacturingDurationPercent(
    const ProfileState &profile,
    const ContentRegistry &content) noexcept
{
    const RegionalBaseSiteDefinition *site = activeSite(profile, content);
    if (site == nullptr)
    {
        return 100U;
    }
    const auto state = profile.regionalOperations.baseSites.find(site->id);
    return state != profile.regionalOperations.baseSites.end() &&
            state->second.uniqueFeatureRepaired
        ? site->uniqueFeatureManufacturingDurationPercent
        : 100U;
}

std::uint32_t applyActiveBaseSiteManufacturingDuration(
    std::uint32_t durationMinutes,
    const ProfileState &profile,
    const ContentRegistry &content) noexcept
{
    const std::uint64_t percent =
        activeBaseSiteManufacturingDurationPercent(profile, content);
    const std::uint64_t adjusted =
        (static_cast<std::uint64_t>(durationMinutes) * percent + 99U) / 100U;
    return static_cast<std::uint32_t>(std::max<std::uint64_t>(1U, adjusted));
}
