#include "base_population_domain.h"

#include "base_emergency_power_domain.h"
#include "recovery_task_domain.h"

#include <algorithm>
#include <limits>
#include <utility>

namespace
{
BaseRestPlan failure(
    DomainErrorCode error,
    std::string message,
    ProfileRevision revision)
{
    return {false, error, std::move(message), revision};
}
}

std::uint32_t dailyRationsForResidents(std::uint32_t residents, BaseRationPolicy policy) noexcept
{
    const auto standard = static_cast<std::uint64_t>(residents) * kRationsPerResidentPerDay;
    // n - floor(n/4) is ceil(3n/4) without an overflowing multiplication.
    const auto demand = policy == BaseRationPolicy::Conserve ? standard - standard / 4 : standard;
    return static_cast<std::uint32_t>(std::min<std::uint64_t>(demand, std::numeric_limits<std::uint32_t>::max()));
}

BasePopulationProjection projectBasePopulation(
    const BasePopulationState &state) noexcept
{
    const std::uint32_t shortfall = state.ordinaryResidents > state.bedCapacity
        ? state.ordinaryResidents - state.bedCapacity
        : 0U;
    const std::uint32_t healthy =
        state.ordinaryResidents > state.injuredResidents
            ? state.ordinaryResidents - state.injuredResidents
            : 0U;
    return {
        state.ordinaryResidents,
        state.bedCapacity,
        shortfall,
        dailyRationsForResidents(state.ordinaryResidents, BaseRationPolicy::Standard),
        state.injuredResidents,
        healthy};
}

BaseResourceBundle populationAdjustedDailyDemand(
    const BasePopulationState &state) noexcept
{
    BaseResourceBundle demand = kBaseDailyDemand;
    demand.food = projectBasePopulation(state).dailyRationDemand;
    return demand;
}

BaseRationPolicyPreview previewBaseRationPolicy(const ProfileState &profile, BaseRationPolicy policy) noexcept
{
    auto population = projectBasePopulation(profile.basePopulation);
    const std::uint32_t standard = population.dailyRationDemand;
    const bool conserve = policy == BaseRationPolicy::Conserve;
    if (conserve) population.dailyRationDemand = dailyRationsForResidents(population.ordinaryResidents, policy);
    auto demand = kBaseDailyDemand;
    demand.food = population.dailyRationDemand;
    return {policy, population, demand, standard, conserve ? BaseMoraleTier::Stable : BaseMoraleTier::High};
}

BaseRestPlan queryBaseRest(
    const ProfileState &profile,
    const BaseRestCommand &command)
{
    if (!profile.homeFounding.established)
        return failure(DomainErrorCode::IllegalDestination,
            "Establish your main base before resting", profile.revision);
    if (profile.activeBaseDefense)
        return failure(DomainErrorCode::IllegalDestination,
            "Base rest is unavailable during realtime defense", profile.revision);
    if (profile.pendingRaid.has_value())
    {
        return failure(
            DomainErrorCode::IllegalDestination,
            "Base rest is unavailable during a Raid",
            profile.revision);
    }
    if (command.hours == 0U || command.hours > kMaximumBaseRestHours)
    {
        return failure(
            DomainErrorCode::InvalidQuantity,
            "Base rest must be between 1 and 12 hours",
            profile.revision);
    }
    if (profile.revision == std::numeric_limits<ProfileRevision>::max())
    {
        return failure(
            DomainErrorCode::RevisionOverflow,
            "profile revision cannot advance",
            profile.revision);
    }
    const std::uint64_t minutes =
        static_cast<std::uint64_t>(command.hours) * kWorldMinutesPerHour;
    WorldClockState arrivalState = profile.worldClock;
    const WorldClockProjection before = projectWorldClock(arrivalState);
    const WorldClockAdvanceResult advanced = advanceWorldClock(
        arrivalState, minutes);
    if (advanced.minutesApplied != minutes)
    {
        return failure(
            DomainErrorCode::Capacity,
            "world clock cannot advance by the requested rest",
            profile.revision);
    }
    const auto rations = previewBaseRationPolicy(profile);
    return {
        true,
        DomainErrorCode::None,
        {},
        profile.revision,
        command.hours,
        minutes,
        projectWorldClock(arrivalState),
        projectWorldClock(arrivalState).completedDays - before.completedDays,
        rations.population,
        rations.dailyDemand};
}

BaseRestReceipt executeBaseRest(
    ProfileState &profile,
    const ContentRegistry &content,
    const BaseRestCommand &command,
    const CommandContext &context)
{
    if (profile.activeBaseDefense)
        return {false, false, DomainErrorCode::IllegalDestination,
                "Base rest is unavailable during realtime defense", profile.revision};
    if (context.transactionId.empty())
    {
        return {false, false, DomainErrorCode::InvalidTransaction,
                "transaction ID must not be empty", profile.revision};
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
    const BaseRestPlan plan = queryBaseRest(profile, command);
    if (!plan.canCommit)
    {
        return {false, false, plan.error, plan.message, profile.revision};
    }

    ProfileState candidate = profile;
    const WorldClockAdvanceResult advanced = advanceWorldClock(
        candidate.worldClock, plan.worldMinutes);
    const BaseWorldWorkAdvanceResult work = synchronizeBaseWorkThrough(candidate, content, advanced.minutesApplied);
    static_cast<void>(applyRecoveryTaskThrough(candidate));
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
    return {
        true,
        false,
        DomainErrorCode::None,
        {},
        profile.revision,
        advanced.minutesApplied,
        work.demand.cyclesResolved,
        work.demand.latestShortfall,
        work.construction,
        work.manufacturing,
        work.residentTreatment};
}
