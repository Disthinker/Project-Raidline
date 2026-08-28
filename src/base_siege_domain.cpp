#include "base_siege_domain.h"

#include "base_workforce_domain.h"

#include <algorithm>
#include <array>
#include <limits>

namespace
{
std::uint64_t rawThreatTotal(const BaseSiegeState &state) noexcept
{
    return static_cast<std::uint64_t>(state.raidThreatUnits) +
        state.populationThreatUnits + state.siteThreatUnits;
}

std::uint32_t addThreatWithinSharedCapacity(
    BaseSiegeState &state,
    std::uint32_t &source,
    std::uint64_t requested) noexcept
{
    const std::uint64_t total = rawThreatTotal(state);
    const std::uint64_t available = total < kBaseSiegeThreatThreshold
        ? kBaseSiegeThreatThreshold - total
        : 0U;
    const std::uint32_t added = static_cast<std::uint32_t>(
        std::min(requested, available));
    source += added;
    return added;
}

void scaleThreatSourcesToTotal(
    BaseSiegeState &state,
    std::uint32_t targetTotal) noexcept
{
    const std::uint64_t total = rawThreatTotal(state);
    if (total == 0U || total == targetTotal)
    {
        return;
    }
    std::array<std::uint32_t *, 3> sources{
        &state.raidThreatUnits,
        &state.populationThreatUnits,
        &state.siteThreatUnits};
    std::array<std::uint64_t, 3> remainders{};
    std::uint32_t assigned{};
    for (std::size_t index{}; index < sources.size(); ++index)
    {
        const std::uint64_t scaled =
            static_cast<std::uint64_t>(*sources[index]) * targetTotal;
        *sources[index] = static_cast<std::uint32_t>(scaled / total);
        remainders[index] = scaled % total;
        assigned += *sources[index];
    }
    while (assigned < targetTotal)
    {
        const auto largest = std::max_element(
            remainders.begin(), remainders.end());
        ++*sources[static_cast<std::size_t>(
            std::distance(remainders.begin(), largest))];
        *largest = 0U;
        ++assigned;
    }
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
    const std::uint64_t total = rawThreatTotal(state);
    return static_cast<std::uint32_t>(std::min<std::uint64_t>(
        kBaseSiegeThreatThreshold, total));
}

BaseThreatProjection projectBaseThreat(const ProfileState &profile) noexcept
{
    const BaseSiegeState &state = profile.baseSiege;
    const std::uint32_t total = totalBaseThreat(state);
    const std::uint64_t safeRemaining =
        state.safeUntilWorldMinute > profile.worldClock.elapsedWorldMinutes
        ? state.safeUntilWorldMinute - profile.worldClock.elapsedWorldMinutes
        : 0U;
    const bool siegeQueued =
        total >= kBaseSiegeThreatThreshold && !state.warningActive &&
        safeRemaining > 0U;
    BaseThreatTier tier = total < 34U
        ? BaseThreatTier::Low
        : total < 67U ? BaseThreatTier::Elevated
                      : total < kBaseSiegeThreatThreshold
            ? BaseThreatTier::Critical
            : siegeQueued ? BaseThreatTier::Queued
                          : BaseThreatTier::Warning;
    if (state.warningActive)
    {
        tier = BaseThreatTier::Warning;
    }
    return {
        tier,
        total,
        state.raidThreatUnits,
        state.populationThreatUnits,
        state.siteThreatUnits,
        safeRemaining,
        siegeQueued,
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
    const std::uint32_t populationAdded = addThreatWithinSharedCapacity(
        state, state.populationThreatUnits, days * populationPerDay);
    const std::uint32_t siteAdded = addThreatWithinSharedCapacity(
        state, state.siteThreatUnits, days * sitePerDay);
    state.resolvedDayCount = completedDays;
    return {
        true,
        days,
        populationAdded,
        siteAdded};
}

void applySettledRaidBaseThreat(ProfileState &profile) noexcept
{
    static_cast<void>(addThreatWithinSharedCapacity(
        profile.baseSiege,
        profile.baseSiege.raidThreatUnits,
        kBaseSiegeRaidThreatUnits));
}

std::uint32_t applyBasePerimeterSweepThreatReduction(
    ProfileState &profile,
    std::uint32_t requestedUnits) noexcept
{
    const std::uint32_t before = totalBaseThreat(profile.baseSiege);
    const std::uint32_t after = requestedUnits >= before
        ? 0U
        : before - requestedUnits;
    scaleThreatSourcesToTotal(profile.baseSiege, after);
    if (after < kBaseSiegeThreatThreshold)
    {
        profile.baseSiege.warningActive = false;
        profile.baseSiege.warningRemainingSeconds = 0U;
    }
    return before - after;
}

void normalizeBaseThreatCapacity(BaseSiegeState &state) noexcept
{
    if (rawThreatTotal(state) > kBaseSiegeThreatThreshold)
    {
        scaleThreatSourcesToTotal(state, kBaseSiegeThreatThreshold);
    }
}

BasePerimeterSweepPlan queryBasePerimeterSweep(
    const ProfileState &profile,
    const ContentRegistry &content) noexcept
{
    BasePerimeterSweepPlan plan;
    plan.revision = profile.revision;
    plan.currentThreatUnits = totalBaseThreat(profile.baseSiege);
    if (profile.pendingRaid.has_value())
    {
        plan.error = DomainErrorCode::IllegalDestination;
        plan.message = "Base perimeter sweep is unavailable during a Raid";
        return plan;
    }
    if (profile.baseSiege.warningActive)
    {
        plan.error = DomainErrorCode::IllegalDestination;
        plan.message = "Base siege warning must be resolved first";
        return plan;
    }
    if (plan.currentThreatUnits < kBasePerimeterSweepMinimumThreat)
    {
        plan.error = DomainErrorCode::IllegalDestination;
        plan.message = "Base threat is below the perimeter sweep threshold";
        return plan;
    }
    if (profile.revision == std::numeric_limits<ProfileRevision>::max())
    {
        plan.error = DomainErrorCode::RevisionOverflow;
        plan.message = "profile revision cannot advance";
        return plan;
    }
    const RegionalBaseSiteDefinition *site = activeSite(profile, content);
    if (site == nullptr)
    {
        plan.error = DomainErrorCode::InvalidProfile;
        plan.message = "active Base site is unavailable";
        return plan;
    }
    plan.baseSiteDefinitionId = site->id;
    plan.mapDefinitionId = site->perimeterSweepMapDefinitionId;
    plan.threatReductionUnits = site->perimeterSweepThreatReductionUnits;
    ProfileState projected = profile;
    applySettledRaidBaseThreat(projected);
    static_cast<void>(applyBasePerimeterSweepThreatReduction(
        projected, plan.threatReductionUnits));
    plan.projectedThreatAfterSettlement = totalBaseThreat(
        projected.baseSiege);
    plan.canDeploy = true;
    return plan;
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
    case BaseThreatTier::Queued: return "QUEUED";
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
