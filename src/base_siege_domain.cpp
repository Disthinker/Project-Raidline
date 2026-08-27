#include "base_siege_domain.h"

#include "base_workforce_domain.h"

#include <algorithm>
#include <limits>

namespace
{
std::uint32_t saturatedThreatAdd(
    std::uint32_t current,
    std::uint64_t added) noexcept
{
    return static_cast<std::uint32_t>(std::min<std::uint64_t>(
        kBaseSiegeThreatThreshold,
        static_cast<std::uint64_t>(current) + added));
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

std::uint32_t autoDefenseSecurityRequirement(
    const ProfileState &profile,
    const ContentRegistry &content) noexcept
{
    std::uint32_t required = 8U +
        (profile.basePopulation.ordinaryResidents + 3U) / 4U;
    if (const RegionalBaseSiteDefinition *site = activeSite(profile, content))
    {
        required += site->dailyBaseThreatUnits > 1U ? 4U : 0U;
    }
    if (profile.baseMorale.tier == BaseMoraleTier::Low)
    {
        required += 4U;
    }
    else if (profile.baseMorale.tier == BaseMoraleTier::High)
    {
        required = required > 2U ? required - 2U : required;
    }
    return std::clamp(required, 8U, 24U);
}

std::uint32_t removeOneUnprotectedResident(ProfileState &profile) noexcept
{
    BasePopulationState &population = profile.basePopulation;
    if (population.ordinaryResidents <= kBaseSiegeMinimumResidents)
    {
        return 0U;
    }
    if (profile.baseConstruction.activeProject.has_value())
    {
        return 0U;
    }
    const std::size_t general = baseProfessionIndex(
        BaseResidentProfession::General);
    const BaseWorkforceProjection workforce = projectBaseWorkforce(profile);
    if (workforce.availableByProfession[general] == 0U ||
        workforce.availableResidents == 0U)
    {
        return 0U;
    }
    --population.professionResidents[general];
    --population.ordinaryResidents;
    return 1U;
}

void clearThreatForSafetyPeriod(
    ProfileState &profile,
    std::uint32_t days) noexcept
{
    BaseSiegeState &state = profile.baseSiege;
    state.raidThreatUnits = 0U;
    state.populationThreatUnits = 0U;
    state.siteThreatUnits = 0U;
    state.warningActive = false;
    state.warningRemainingSeconds = 0U;
    const std::uint64_t duration =
        static_cast<std::uint64_t>(days) * kWorldMinutesPerDay;
    state.safeUntilWorldMinute =
        profile.worldClock.elapsedWorldMinutes >
                std::numeric_limits<std::uint64_t>::max() - duration
            ? std::numeric_limits<std::uint64_t>::max()
            : profile.worldClock.elapsedWorldMinutes + duration;
}
}

std::uint32_t totalBaseThreat(const BaseSiegeState &state) noexcept
{
    const std::uint64_t total =
        static_cast<std::uint64_t>(state.raidThreatUnits) +
        state.populationThreatUnits + state.siteThreatUnits;
    return static_cast<std::uint32_t>(std::min<std::uint64_t>(
        kBaseSiegeThreatThreshold, total));
}

BaseThreatProjection projectBaseThreat(const ProfileState &profile) noexcept
{
    const BaseSiegeState &state = profile.baseSiege;
    const std::uint32_t total = totalBaseThreat(state);
    BaseThreatTier tier = total < 34U
        ? BaseThreatTier::Low
        : total < 67U ? BaseThreatTier::Elevated
                      : total < kBaseSiegeThreatThreshold
            ? BaseThreatTier::Critical
            : BaseThreatTier::Warning;
    if (state.warningActive)
    {
        tier = BaseThreatTier::Warning;
    }
    const std::uint64_t safeRemaining =
        state.safeUntilWorldMinute > profile.worldClock.elapsedWorldMinutes
        ? state.safeUntilWorldMinute - profile.worldClock.elapsedWorldMinutes
        : 0U;
    return {
        tier,
        total,
        state.raidThreatUnits,
        state.populationThreatUnits,
        state.siteThreatUnits,
        safeRemaining,
        state.warningActive,
        state.warningRemainingSeconds,
        state.autoDefensePresetSaved,
        state.warningActive && state.warningRemainingSeconds == 0U &&
            !state.autoDefensePresetSaved};
}

BaseThreatAdvanceResult synchronizeBaseThreatThrough(
    ProfileState &profile,
    const ContentRegistry &content) noexcept
{
    BaseSiegeState &state = profile.baseSiege;
    const std::uint64_t completedDays =
        projectWorldClock(profile.worldClock).completedDays;
    if (completedDays <= state.resolvedDayCount)
    {
        return {};
    }
    const std::uint64_t days = completedDays - state.resolvedDayCount;
    const std::uint32_t populationPerDay = std::max(
        1U, (profile.basePopulation.ordinaryResidents + 9U) / 10U);
    const RegionalBaseSiteDefinition *site = activeSite(profile, content);
    const std::uint32_t sitePerDay = site != nullptr
        ? site->dailyBaseThreatUnits
        : 1U;
    const std::uint32_t previousPopulation = state.populationThreatUnits;
    const std::uint32_t previousSite = state.siteThreatUnits;
    state.populationThreatUnits = saturatedThreatAdd(
        state.populationThreatUnits,
        days * populationPerDay);
    state.siteThreatUnits = saturatedThreatAdd(
        state.siteThreatUnits,
        days * sitePerDay);
    state.resolvedDayCount = completedDays;
    return {
        true,
        days,
        state.populationThreatUnits - previousPopulation,
        state.siteThreatUnits - previousSite};
}

void applySettledRaidBaseThreat(ProfileState &profile) noexcept
{
    profile.baseSiege.raidThreatUnits = saturatedThreatAdd(
        profile.baseSiege.raidThreatUnits,
        kBaseSiegeRaidThreatUnits);
}

bool activateBaseSiegeWarningIfEligible(ProfileState &profile) noexcept
{
    BaseSiegeState &state = profile.baseSiege;
    if (state.warningActive || profile.pendingRaid.has_value() ||
        totalBaseThreat(state) < kBaseSiegeThreatThreshold ||
        profile.worldClock.elapsedWorldMinutes < state.safeUntilWorldMinute)
    {
        return false;
    }
    state.warningActive = true;
    state.warningRemainingSeconds = kBaseSiegeWarningSeconds;
    if (state.siegeSequence != std::numeric_limits<std::uint64_t>::max())
    {
        ++state.siegeSequence;
    }
    return true;
}

bool advanceBaseSiegeWarning(
    ProfileState &profile,
    std::uint32_t elapsedSeconds) noexcept
{
    BaseSiegeState &state = profile.baseSiege;
    if (!state.warningActive || elapsedSeconds == 0U ||
        state.warningRemainingSeconds == 0U)
    {
        return false;
    }
    state.warningRemainingSeconds = elapsedSeconds >=
            state.warningRemainingSeconds
        ? 0U
        : state.warningRemainingSeconds - elapsedSeconds;
    return true;
}

BaseAutoDefensePlan queryBaseAutoDefense(
    const ProfileState &profile,
    const ContentRegistry &content) noexcept
{
    if (!profile.baseSiege.warningActive)
    {
        return {false, DomainErrorCode::IllegalDestination,
                "Base is not under siege warning", profile.revision};
    }
    if (profile.pendingRaid.has_value())
    {
        return {false, DomainErrorCode::IllegalDestination,
                "automatic defense is unavailable during a Raid",
                profile.revision};
    }
    if (profile.revision == std::numeric_limits<ProfileRevision>::max())
    {
        return {false, DomainErrorCode::RevisionOverflow,
                "profile revision cannot advance", profile.revision};
    }
    const std::uint32_t required = autoDefenseSecurityRequirement(
        profile, content);
    return {true, DomainErrorCode::None, {}, profile.revision,
            required, profile.baseResources.pool.security,
            profile.baseResources.pool.security >= required};
}

BaseAutoDefenseReceipt executeBaseAutoDefense(
    ProfileState &profile,
    const ContentRegistry &content,
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
                profile.baseSiege.lastOutcome,
                profile.baseSiege.lastSecuritySpent,
                profile.baseSiege.lastPopulationLost,
                profile.baseSiege.safeUntilWorldMinute};
    }
    if (context.expectedRevision != profile.revision)
    {
        return {false, false, DomainErrorCode::StaleRevision,
                "profile revision is stale", profile.revision};
    }
    const BaseAutoDefensePlan plan = queryBaseAutoDefense(profile, content);
    if (!plan.canCommit)
    {
        return {false, false, plan.error, plan.message, profile.revision};
    }

    ProfileState candidate = profile;
    BaseSiegeState &state = candidate.baseSiege;
    state.autoDefensePresetSaved = true;
    state.lastSecuritySpent = std::min(
        plan.requiredSecurity, candidate.baseResources.pool.security);
    candidate.baseResources.pool.security -= state.lastSecuritySpent;
    state.lastPopulationLost = 0U;
    if (plan.projectedSuccess)
    {
        state.lastOutcome = BaseSiegeOutcome::Defended;
        candidate.baseConstruction.materialUnits =
            std::min<std::uint32_t>(
                content.maximumBaseConstructionMaterials(),
                candidate.baseConstruction.materialUnits + 8U);
        if (candidate.baseMorale.pendingPositiveEventCount !=
            std::numeric_limits<std::uint64_t>::max())
        {
            ++candidate.baseMorale.pendingPositiveEventCount;
        }
        clearThreatForSafetyPeriod(candidate, kBaseSiegeSuccessSafeDays);
    }
    else
    {
        state.lastOutcome = BaseSiegeOutcome::SoftFailure;
        candidate.baseResources.pool.food =
            candidate.baseResources.pool.food > 5U
            ? candidate.baseResources.pool.food - 5U : 0U;
        candidate.baseResources.pool.hygiene =
            candidate.baseResources.pool.hygiene > 5U
            ? candidate.baseResources.pool.hygiene - 5U : 0U;
        candidate.baseResources.pool.morale =
            candidate.baseResources.pool.morale > 5U
            ? candidate.baseResources.pool.morale - 5U : 0U;
        state.lastPopulationLost = removeOneUnprotectedResident(candidate);
        if (candidate.baseMorale.pendingNegativeEventCount !=
            std::numeric_limits<std::uint64_t>::max())
        {
            ++candidate.baseMorale.pendingNegativeEventCount;
        }
        clearThreatForSafetyPeriod(candidate, kBaseSiegeFailureSafeDays);
    }
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
    return {true, false, DomainErrorCode::None, {}, profile.revision,
            profile.baseSiege.lastOutcome,
            profile.baseSiege.lastSecuritySpent,
            profile.baseSiege.lastPopulationLost,
            profile.baseSiege.safeUntilWorldMinute};
}

const char *baseThreatTierName(BaseThreatTier tier) noexcept
{
    switch (tier)
    {
    case BaseThreatTier::Low: return "LOW";
    case BaseThreatTier::Elevated: return "ELEVATED";
    case BaseThreatTier::Critical: return "CRITICAL";
    case BaseThreatTier::Warning: return "WARNING";
    }
    return "LOW";
}

const char *baseSiegeOutcomeName(BaseSiegeOutcome outcome) noexcept
{
    switch (outcome)
    {
    case BaseSiegeOutcome::None: return "NONE";
    case BaseSiegeOutcome::Defended: return "DEFENDED";
    case BaseSiegeOutcome::SoftFailure: return "SOFT FAILURE";
    }
    return "NONE";
}
