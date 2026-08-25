#include "base_population_domain.h"

#include "base_construction_domain.h"

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

BasePopulationProjection projectBasePopulation(
    const BasePopulationState &state) noexcept
{
    const std::uint32_t shortfall = state.ordinaryResidents > state.bedCapacity
        ? state.ordinaryResidents - state.bedCapacity
        : 0U;
    const std::uint64_t demand =
        static_cast<std::uint64_t>(state.ordinaryResidents) *
        kRationsPerResidentPerDay;
    return {
        state.ordinaryResidents,
        state.bedCapacity,
        shortfall,
        demand > std::numeric_limits<std::uint32_t>::max()
            ? std::numeric_limits<std::uint32_t>::max()
            : static_cast<std::uint32_t>(demand)};
}

BaseResourceBundle populationAdjustedDailyDemand(
    const BasePopulationState &state) noexcept
{
    BaseResourceBundle demand = kBaseDailyDemand;
    demand.food = projectBasePopulation(state).dailyRationDemand;
    return demand;
}

BaseRestPlan queryBaseRest(
    const ProfileState &profile,
    const BaseRestCommand &command)
{
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
    const BasePopulationProjection population = projectBasePopulation(
        profile.basePopulation);
    return {
        true,
        DomainErrorCode::None,
        {},
        profile.revision,
        command.hours,
        minutes,
        projectWorldClock(arrivalState),
        projectWorldClock(arrivalState).completedDays - before.completedDays,
        population,
        populationAdjustedDailyDemand(profile.basePopulation)};
}

BaseRestReceipt executeBaseRest(
    ProfileState &profile,
    const ContentRegistry &content,
    const BaseRestCommand &command,
    const CommandContext &context)
{
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
    const BaseDailyDemandResult demand = applyBaseDailyDemandThrough(
        candidate.baseResources,
        advanced.completedDaysAfter,
        plan.dailyDemand);
    static_cast<void>(synchronizeBasePriorityThrough(candidate, content));
    static_cast<void>(applyBaseConstructionThrough(candidate, content));
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
        demand.cyclesResolved,
        demand.latestShortfall};
}
