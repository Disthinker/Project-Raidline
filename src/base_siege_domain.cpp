#include "base_siege_domain.h"
#include "base_defense_ownership.h"

#include "base_workforce_domain.h"
#include "home_perimeter_domain.h"

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
    profile.baseDefenseWarning.reset();
    const std::uint64_t duration =
        static_cast<std::uint64_t>(days) * kWorldMinutesPerDay;
    state.safeUntilWorldMinute =
        profile.worldClock.elapsedWorldMinutes >
                std::numeric_limits<std::uint64_t>::max() - duration
            ? std::numeric_limits<std::uint64_t>::max()
            : profile.worldClock.elapsedWorldMinutes + duration;
}

void applyDefenseOutcome(ProfileState &profile,
                         const ContentRegistry &content, bool success)
{
    BaseSiegeState &state = profile.baseSiege;
    state.lastPopulationLost = 0U;
    if (success)
    {
        state.lastOutcome = BaseSiegeOutcome::Defended;
        profile.baseConstruction.materialUnits = std::min<std::uint32_t>(
            content.maximumBaseConstructionMaterials(),
            profile.baseConstruction.materialUnits + 8U);
        if (profile.baseMorale.pendingPositiveEventCount !=
            std::numeric_limits<std::uint64_t>::max())
            ++profile.baseMorale.pendingPositiveEventCount;
        clearThreatForSafetyPeriod(profile, kBaseSiegeSuccessSafeDays);
    }
    else
    {
        state.lastOutcome = BaseSiegeOutcome::SoftFailure;
        auto subtract = [](std::uint32_t &value) { value = value > 5U ? value - 5U : 0U; };
        subtract(profile.baseResources.pool.food);
        subtract(profile.baseResources.pool.hygiene);
        subtract(profile.baseResources.pool.morale);
        state.lastPopulationLost = removeOneUnprotectedResident(profile);
        if (profile.baseMorale.pendingNegativeEventCount !=
            std::numeric_limits<std::uint64_t>::max())
            ++profile.baseMorale.pendingNegativeEventCount;
        clearThreatForSafetyPeriod(profile, kBaseSiegeFailureSafeDays);
    }
    state.lastResolvedSequence = state.siegeSequence;
}

BaseAutoDefenseReceipt defenseReceipt(const ProfileState &profile,
                                      bool already = false)
{
    return {true, already, DomainErrorCode::None, {}, profile.revision,
            profile.baseSiege.lastOutcome, profile.baseSiege.lastSecuritySpent,
            profile.baseSiege.lastPopulationLost,
            profile.baseSiege.safeUntilWorldMinute};
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
    if (profile.baseSiege.warningActive || profile.activeBaseDefense)
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
    if (state.warningActive || profile.activeBaseDefense || profile.pendingRaid.has_value() ||
        state.siegeSequence == std::numeric_limits<std::uint64_t>::max() ||
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
    if (profile.activeBaseDefense || !state.warningActive || elapsedSeconds == 0U ||
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
    if (profile.activeBaseDefense || !profile.baseSiege.warningActive)
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
    if (!profile.baseSiege.warningActive && !profile.activeBaseDefense &&
        profile.baseSiege.siegeSequence != 0U &&
        profile.baseSiege.lastResolvedSequence == profile.baseSiege.siegeSequence)
    {
        return defenseReceipt(profile, true);
    }
    if (profile.committedTransactions.contains(context.transactionId))
    {
        if (profile.activeBaseDefense || profile.baseSiege.warningActive)
            return {false, false, DomainErrorCode::InvalidTransaction,
                    "transaction ID belongs to another defense operation", profile.revision};
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
    applyDefenseOutcome(candidate, content, plan.projectedSuccess);
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

std::string baseSiegeEventId(const ProfileState &profile)
{
    return profile.profileId + "-base-siege-" +
        std::to_string(profile.baseSiege.siegeSequence);
}

BaseRealtimeDefensePlan queryBaseRealtimeDefenseStart(
    const ProfileState &profile, const ContentRegistry &content,
    const BaseDefenseSnapshot &snapshot)
{
    auto reject = [&](DomainErrorCode error, std::string message) {
        return BaseRealtimeDefensePlan{false, error, std::move(message), profile.revision};
    };
    if (!profile.baseSiege.warningActive || profile.activeBaseDefense ||
        profile.pendingRaid || !profile.homeFounding.established)
        return reject(DomainErrorCode::IllegalDestination,
                      "Realtime defense requires an established Base under warning");
    if (profile.baseDefenseWarning)
    {
        if (!validateDefenseWarning(profile, content).valid || !profile.baseDefenseWarning->layout ||
            snapshot.rulesVersion != kFortifiedBaseDefenseRulesVersion ||
            !matchesDefenseWarning(snapshot, *profile.baseDefenseWarning->layout) ||
            !validateDefenseFortificationOwnership(profile, content, snapshot).valid ||
            std::any_of(snapshot.fortifications.begin(), snapshot.fortifications.end(),
                [](const auto &f) { return f.initialDurability != f.durability; }))
            return reject(DomainErrorCode::InvalidProfile, "Defense differs from frozen warning or owners");
    }
    else if (snapshot.rulesVersion != kBaseDefenseRulesVersion)
        return reject(DomainErrorCode::IllegalDestination,
                      "Fortified defense requires a frozen warning");
    if (profile.revision == std::numeric_limits<ProfileRevision>::max())
        return reject(DomainErrorCode::RevisionOverflow, "profile revision cannot advance");
    const auto *site = activeSite(profile, content);
    if (site == nullptr || snapshot.siteDefinitionId != site->id.value() ||
        snapshot.eventId != baseSiegeEventId(profile) ||
        snapshot.siegeSequence != profile.baseSiege.siegeSequence ||
        snapshot.siegeSequence <= profile.baseSiege.lastResolvedSequence ||
        (!profile.baseDefenseWarning &&
         (snapshot.frozenPopulation != profile.basePopulation.ordinaryResidents ||
          snapshot.frozenMoraleTier != static_cast<std::uint32_t>(profile.baseMorale.tier) ||
          snapshot.frozenSiteThreat != site->dailyBaseThreatUnits)))
        return reject(DomainErrorCode::InvalidProfile, "Defense event identity or frozen inputs are stale");
    const auto plot = profile.homeFounding.plots.find(site->id);
    const std::string expectedPlot = plot == profile.homeFounding.plots.end() ? "" : plot->second;
    if (snapshot.plotId != expectedPlot || snapshot.elapsedSeconds != 0.0F ||
        snapshot.spawnedEnemyCount != 0U || !snapshot.enemies.empty() ||
        !snapshot.killedIds.empty() || !snapshot.breachedIds.empty())
        return reject(DomainErrorCode::InvalidProfile, "Defense start must use a fresh frozen layout");
    std::string message;
    if (!validateBaseDefenseSnapshot(snapshot, message))
        return reject(DomainErrorCode::InvalidProfile, std::move(message));
    return {true, DomainErrorCode::None, {}, profile.revision};
}

BaseAutoDefenseReceipt executeBaseRealtimeDefenseStart(
    ProfileState &profile, const ContentRegistry &content,
    const BaseDefenseSnapshot &snapshot, const CommandContext &context)
{
    if (context.transactionId.empty())
        return {false, false, DomainErrorCode::InvalidTransaction, "transaction ID is empty", profile.revision};
    if (profile.activeBaseDefense && profile.activeBaseDefense->eventId == snapshot.eventId &&
        profile.committedTransactions.contains(context.transactionId))
        return {true, true, DomainErrorCode::None, {}, profile.revision};
    if (context.expectedRevision != profile.revision)
        return {false, false, DomainErrorCode::StaleRevision, "profile revision is stale", profile.revision};
    if (profile.committedTransactions.contains(context.transactionId))
        return {false, false, DomainErrorCode::InvalidTransaction, "transaction ID has another result", profile.revision};
    const auto plan = queryBaseRealtimeDefenseStart(profile, content, snapshot);
    if (!plan.canCommit)
        return {false, false, plan.error, plan.message, profile.revision};
    ProfileState candidate = profile;
    candidate.activeBaseDefense = snapshot;
    // The Base event takes over the existing outing without invoking its
    // return/rescue settlement. Personal Loot stays where it is, and a later
    // defense rescue is therefore charged once instead of 240 + 90 minutes.
    if (candidate.homePerimeter.activeOuting)
    {
        const std::string handoff = snapshot.eventId + "-perimeter-handoff";
        candidate.homePerimeter.committedResults.insert(handoff);
        candidate.committedTransactions.insert(handoff);
        candidate.homePerimeter.activeOuting.reset();
    }
    candidate.baseSiege.warningActive = false;
    candidate.baseSiege.warningRemainingSeconds = 0U;
    candidate.committedTransactions.insert(context.transactionId);
    ++candidate.revision;
    const auto validation = validateProfileState(candidate, content);
    if (!validation.valid)
        return {false, false, DomainErrorCode::InvalidProfile, validation.message, profile.revision};
    profile = std::move(candidate);
    return {true, false, DomainErrorCode::None, {}, profile.revision};
}

BaseAutoDefenseReceipt executeBaseRealtimeDefenseSettlement(
    ProfileState &profile, const ContentRegistry &content,
    std::string_view eventId, BaseDefenseEndReason reason,
    const CommandContext &context)
{
    if (context.transactionId.empty())
        return {false, false, DomainErrorCode::InvalidTransaction, "transaction ID is empty", profile.revision};
    if (!profile.activeBaseDefense && profile.baseSiege.lastResolvedSequence != 0U &&
        eventId == profile.profileId + "-base-siege-" +
            std::to_string(profile.baseSiege.lastResolvedSequence))
        return defenseReceipt(profile, true);
    if (context.expectedRevision != profile.revision)
        return {false, false, DomainErrorCode::StaleRevision, "profile revision is stale", profile.revision};
    if (profile.committedTransactions.contains(context.transactionId))
        return {false, false, DomainErrorCode::InvalidTransaction, "transaction ID has another result", profile.revision};
    if (!profile.activeBaseDefense || profile.activeBaseDefense->eventId != eventId || profile.pendingRaid)
        return {false, false, DomainErrorCode::IllegalDestination, "Defense event is not active", profile.revision};
    if (profile.revision == std::numeric_limits<ProfileRevision>::max())
        return {false, false, DomainErrorCode::RevisionOverflow, "profile revision cannot advance", profile.revision};
    const auto &snapshot = *profile.activeBaseDefense;
    std::string message;
    if (!validateBaseDefenseSnapshot(snapshot, message) ||
        !validateDefenseFortificationOwnership(profile, content, snapshot).valid)
        return {false, false, DomainErrorCode::InvalidProfile, message, profile.revision};
    std::size_t planned{};
    for (const auto &wave : snapshot.wavePlans) planned += wave.enemyIds.size();
    const bool breached = snapshot.breachedIds.size() >= snapshot.breachLimit;
    const bool down = profile.currentHealth <= 0;
    const bool completed = !breached && !down && snapshot.enemies.empty() &&
        snapshot.killedIds.size() + snapshot.breachedIds.size() == planned;
    if ((reason == BaseDefenseEndReason::Completed && !completed) ||
        (reason == BaseDefenseEndReason::PlayerDown && !down) ||
        (reason == BaseDefenseEndReason::Breached && !breached) ||
        static_cast<std::uint32_t>(reason) > static_cast<std::uint32_t>(BaseDefenseEndReason::Abandoned))
        return {false, false, DomainErrorCode::IllegalDestination, "Defense result has not occurred", profile.revision};
    ProfileState candidate = profile;
    // Downed-player rescue is Base-only: it never invokes Raid loss or the
    // complete perimeter-return transaction (which would charge time twice).
    if (down)
    {
        candidate.currentHealth = kHomePerimeterRescueHealth;
        candidate.medicalStatus = {};
        if (advanceWorldClock(candidate.worldClock, kHomePerimeterRescueMinutes).minutesApplied !=
            kHomePerimeterRescueMinutes)
            return {false, false, DomainErrorCode::IllegalDestination,
                    "Base defense rescue time cannot advance", profile.revision};
    }
    candidate.baseSiege.lastSecuritySpent = 0U;
    applyDefenseOutcome(candidate, content, reason == BaseDefenseEndReason::Completed);
    candidate.activeBaseDefense.reset();
    candidate.committedTransactions.insert(context.transactionId);
    ++candidate.revision;
    const auto validation = validateProfileState(candidate, content);
    if (!validation.valid)
        return {false, false, DomainErrorCode::InvalidProfile, validation.message, profile.revision};
    profile = std::move(candidate);
    return defenseReceipt(profile);
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
