#include "base_morale_domain.h"

#include <algorithm>
#include <limits>

namespace
{
std::uint64_t stableProfileHash(std::string_view value) noexcept
{
    std::uint64_t hash = 1469598103934665603ULL;
    for (const unsigned char byte : value)
    {
        hash ^= byte;
        hash *= 1099511628211ULL;
    }
    return hash;
}

void saturatedAdd(std::uint64_t &target, std::uint64_t value) noexcept
{
    target = value > std::numeric_limits<std::uint64_t>::max() - target
        ? std::numeric_limits<std::uint64_t>::max()
        : target + value;
}

std::uint64_t saturatedSum(
    std::uint64_t left,
    std::uint64_t right) noexcept
{
    saturatedAdd(left, right);
    return left;
}

std::uint64_t saturatedMultiply(
    std::uint64_t left,
    std::uint64_t right) noexcept
{
    if (left != 0U &&
        right > std::numeric_limits<std::uint64_t>::max() / left)
    {
        return std::numeric_limits<std::uint64_t>::max();
    }
    return left * right;
}

std::uint32_t tierOrdinal(BaseMoraleTier tier) noexcept
{
    switch (tier)
    {
    case BaseMoraleTier::Low:
        return 0U;
    case BaseMoraleTier::Stable:
        return 1U;
    case BaseMoraleTier::High:
        return 2U;
    }
    return 1U;
}

BaseMoraleTier tierFromOrdinal(std::uint32_t value) noexcept
{
    if (value == 0U)
    {
        return BaseMoraleTier::Low;
    }
    if (value >= 2U)
    {
        return BaseMoraleTier::High;
    }
    return BaseMoraleTier::Stable;
}

std::int32_t boundedScore(
    std::uint64_t positive,
    std::uint64_t negative) noexcept
{
    constexpr std::int32_t limit = 9;
    if (positive >= negative)
    {
        return static_cast<std::int32_t>(std::min<std::uint64_t>(
            positive - negative,
            limit));
    }
    return -static_cast<std::int32_t>(std::min<std::uint64_t>(
        negative - positive,
        limit));
}

std::size_t eventIndex(
    std::string_view profileId,
    std::uint64_t cycleIndex,
    std::size_t eventCount) noexcept
{
    const std::uint64_t offset = stableProfileHash(profileId) % eventCount;
    return static_cast<std::size_t>(
        (offset + cycleIndex % eventCount) % eventCount);
}
}

BaseCommunityEventDefinitionId selectBaseCommunityEvent(
    std::string_view profileId,
    std::uint64_t cycleIndex,
    const ContentRegistry &content)
{
    const auto &events = content.baseCommunityEvents();
    if (events.empty())
    {
        return {};
    }
    return events[eventIndex(profileId, cycleIndex, events.size())].id;
}

BaseCommunityEventSyncResult synchronizeBaseCommunityEventThrough(
    ProfileState &profile,
    const ContentRegistry &content)
{
    const BaseMoraleDefinition &rules = content.baseMorale();
    const auto &events = content.baseCommunityEvents();
    const std::uint64_t completedDays =
        projectWorldClock(profile.worldClock).completedDays;
    const std::uint64_t currentCycle = completedDays / rules.eventCycleDays;
    BaseCommunityEventState &state = profile.baseCommunityEvent;
    if (!state.definitionId.valid())
    {
        state.cycleIndex = currentCycle;
        state.definitionId = selectBaseCommunityEvent(
            profile.profileId,
            currentCycle,
            content);
        return {true, 0U, 0U, 0U};
    }
    if (currentCycle <= state.cycleIndex)
    {
        return {};
    }

    const std::uint64_t advanced = currentCycle - state.cycleIndex;
    std::uint64_t positive{};
    std::uint64_t negative{};
    const std::uint64_t fullRotations = advanced / events.size();
    const std::uint64_t remainder = advanced % events.size();
    std::uint64_t positivePerRotation{};
    std::uint64_t negativePerRotation{};
    for (const BaseCommunityEventDefinition &event : events)
    {
        positivePerRotation += event.moraleEffect > 0 ? 1U : 0U;
        negativePerRotation += event.moraleEffect < 0 ? 1U : 0U;
    }
    positive = saturatedMultiply(fullRotations, positivePerRotation);
    negative = saturatedMultiply(fullRotations, negativePerRotation);
    for (std::uint64_t offset = 1U; offset <= remainder; ++offset)
    {
        const std::uint64_t cycle = state.cycleIndex + offset;
        const BaseCommunityEventDefinition &event = events[eventIndex(
            profile.profileId,
            cycle,
            events.size())];
        saturatedAdd(
            event.moraleEffect > 0 ? positive : negative,
            1U);
    }
    saturatedAdd(profile.baseMorale.pendingPositiveEventCount, positive);
    saturatedAdd(profile.baseMorale.pendingNegativeEventCount, negative);
    state.cycleIndex = currentCycle;
    state.definitionId = selectBaseCommunityEvent(
        profile.profileId,
        currentCycle,
        content);
    return {true, advanced, positive, negative};
}

BaseMoraleAdvanceResult applyBaseMoraleThrough(
    ProfileState &profile,
    const ContentRegistry &content,
    const BaseDailyDemandResult &demand)
{
    BaseMoraleState &state = profile.baseMorale;
    const std::uint64_t completedDays =
        projectWorldClock(profile.worldClock).completedDays;
    if (completedDays <= state.resolvedDayCount)
    {
        return {};
    }
    const std::uint64_t cycles = completedDays - state.resolvedDayCount;
    const BaseMoraleTier previous = state.tier;
    const BasePopulationProjection population = projectBasePopulation(
        profile.basePopulation);
    const std::uint64_t shortageDays = std::max(
        demand.shortageCycleCount,
        population.bedShortfall > 0U ? cycles : 0U);
    const std::uint64_t positiveReasons = saturatedSum(
        state.pendingFulfilledWishCount,
        state.pendingPositiveEventCount);
    const std::uint64_t negativeReasons = saturatedSum(
        state.pendingMissedWishCount,
        state.pendingNegativeEventCount);

    std::uint32_t ordinal = tierOrdinal(state.tier);
    if (shortageDays > 0U || negativeReasons > positiveReasons)
    {
        const std::uint64_t reasonDays = negativeReasons > positiveReasons
            ? negativeReasons - positiveReasons
            : 0U;
        const std::uint64_t fallingDays = std::max(shortageDays, reasonDays);
        const std::uint32_t steps = static_cast<std::uint32_t>(
            std::min<std::uint64_t>({fallingDays, cycles, ordinal}));
        ordinal -= steps;
        state.supportedRecoveryDays = 0U;
    }
    else if (ordinal == 0U)
    {
        const std::uint64_t recovery =
            static_cast<std::uint64_t>(state.supportedRecoveryDays) + cycles;
        if (recovery >= content.baseMorale().recoveryDaysFromLow)
        {
            ordinal = 1U;
            state.supportedRecoveryDays = 0U;
        }
        else
        {
            state.supportedRecoveryDays = static_cast<std::uint32_t>(recovery);
        }
    }
    else if (ordinal == 1U && positiveReasons > negativeReasons)
    {
        ordinal = 2U;
        state.supportedRecoveryDays = 0U;
    }

    state.tier = tierFromOrdinal(ordinal);
    state.trend = ordinal > tierOrdinal(previous)
        ? BaseMoraleTrend::Rising
        : ordinal < tierOrdinal(previous)
            ? BaseMoraleTrend::Falling
            : BaseMoraleTrend::Steady;
    if (state.tier == BaseMoraleTier::Low)
    {
        saturatedAdd(state.consecutiveLowDays, cycles);
    }
    else
    {
        state.consecutiveLowDays = 0U;
    }

    std::uint64_t ledgerNegative = negativeReasons;
    saturatedAdd(ledgerNegative, shortageDays > 0U ? 1U : 0U);
    state.lastLedger = BaseMoraleDailyLedger{
        completedDays,
        demand.latestShortfall,
        population.bedShortfall,
        state.pendingFulfilledWishCount,
        state.pendingMissedWishCount,
        state.pendingPositiveEventCount,
        state.pendingNegativeEventCount,
        boundedScore(positiveReasons, ledgerNegative)};
    state.resolvedDayCount = completedDays;
    state.pendingFulfilledWishCount = 0U;
    state.pendingMissedWishCount = 0U;
    state.pendingPositiveEventCount = 0U;
    state.pendingNegativeEventCount = 0U;
    return {true, cycles, previous, state.tier};
}

BaseDailySystemsResult synchronizeBaseDailySystemsThrough(
    ProfileState &profile,
    const ContentRegistry &content)
{
    BaseDailySystemsResult result;
    const std::uint64_t completedDays =
        projectWorldClock(profile.worldClock).completedDays;
    result.demand = applyBaseDailyDemandWithSupplyThrough(
        profile,
        content,
        completedDays,
        populationAdjustedDailyDemand(profile.basePopulation));
    result.morale = applyBaseMoraleThrough(profile, content, result.demand);
    result.priority = synchronizeBasePriorityThrough(profile, content);
    result.event = synchronizeBaseCommunityEventThrough(profile, content);
    return result;
}

std::uint32_t baseManufacturingDurationPercent(
    BaseMoraleTier tier,
    const BaseMoraleDefinition &definition) noexcept
{
    switch (tier)
    {
    case BaseMoraleTier::Low:
        return definition.lowManufacturingDurationPercent;
    case BaseMoraleTier::Stable:
        return definition.stableManufacturingDurationPercent;
    case BaseMoraleTier::High:
        return definition.highManufacturingDurationPercent;
    }
    return definition.stableManufacturingDurationPercent;
}

std::uint32_t applyBaseMoraleDurationPercent(
    std::uint32_t baseMinutes,
    BaseMoraleTier tier,
    const BaseMoraleDefinition &definition) noexcept
{
    const std::uint64_t scaled =
        static_cast<std::uint64_t>(baseMinutes) *
        baseManufacturingDurationPercent(tier, definition);
    const std::uint64_t rounded = (scaled + 99U) / 100U;
    return static_cast<std::uint32_t>(std::min<std::uint64_t>(
        rounded,
        std::numeric_limits<std::uint32_t>::max()));
}

const char *baseMoraleTierName(BaseMoraleTier tier) noexcept
{
    switch (tier)
    {
    case BaseMoraleTier::Low:
        return "LOW";
    case BaseMoraleTier::Stable:
        return "STABLE";
    case BaseMoraleTier::High:
        return "HIGH";
    }
    return "STABLE";
}

const char *baseMoraleTrendName(BaseMoraleTrend trend) noexcept
{
    switch (trend)
    {
    case BaseMoraleTrend::Falling:
        return "FALLING";
    case BaseMoraleTrend::Steady:
        return "STEADY";
    case BaseMoraleTrend::Rising:
        return "RISING";
    }
    return "STEADY";
}
