#pragma once

#include "base_population_domain.h"

struct BaseMoraleAdvanceResult
{
    bool changed{};
    std::uint64_t daysResolved{};
    BaseMoraleTier previousTier{BaseMoraleTier::Stable};
    BaseMoraleTier currentTier{BaseMoraleTier::Stable};
};

struct BaseCommunityEventSyncResult
{
    bool changed{};
    std::uint64_t cyclesAdvanced{};
    std::uint64_t positiveEventsAdded{};
    std::uint64_t negativeEventsAdded{};
};

struct BaseDailySystemsResult
{
    BaseDailyDemandResult demand;
    BaseMoraleAdvanceResult morale;
    BasePrioritySyncResult priority;
    BaseCommunityEventSyncResult event;
};

[[nodiscard]] BaseCommunityEventDefinitionId selectBaseCommunityEvent(
    std::string_view profileId,
    std::uint64_t cycleIndex,
    const ContentRegistry &content);

[[nodiscard]] BaseCommunityEventSyncResult
synchronizeBaseCommunityEventThrough(
    ProfileState &profile,
    const ContentRegistry &content);

[[nodiscard]] BaseMoraleAdvanceResult applyBaseMoraleThrough(
    ProfileState &profile,
    const ContentRegistry &content,
    const BaseDailyDemandResult &demand);

[[nodiscard]] BaseDailySystemsResult synchronizeBaseDailySystemsThrough(
    ProfileState &profile,
    const ContentRegistry &content);

[[nodiscard]] std::uint32_t baseManufacturingDurationPercent(
    BaseMoraleTier tier,
    const BaseMoraleDefinition &definition) noexcept;

[[nodiscard]] std::uint32_t applyBaseMoraleDurationPercent(
    std::uint32_t baseMinutes,
    BaseMoraleTier tier,
    const BaseMoraleDefinition &definition) noexcept;

[[nodiscard]] const char *baseMoraleTierName(BaseMoraleTier tier) noexcept;
[[nodiscard]] const char *baseMoraleTrendName(BaseMoraleTrend trend) noexcept;

