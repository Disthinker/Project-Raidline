#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <map>

#include "definition_id.h"

enum class RaidIntelligenceCategory : std::uint8_t
{
    Transport,
    Resource,
    Enemy,
    Count
};

inline constexpr std::size_t kRaidIntelligenceCategoryCount =
    static_cast<std::size_t>(RaidIntelligenceCategory::Count);

[[nodiscard]] constexpr std::size_t raidIntelligenceCategoryIndex(
    RaidIntelligenceCategory category) noexcept
{
    return static_cast<std::size_t>(category);
}

struct RaidIntelligenceLoadout
{
    std::array<bool, kRaidIntelligenceCategoryCount> selected{};

    [[nodiscard]] bool has(RaidIntelligenceCategory category) const noexcept
    {
        return selected[raidIntelligenceCategoryIndex(category)];
    }

    void set(RaidIntelligenceCategory category, bool value) noexcept
    {
        selected[raidIntelligenceCategoryIndex(category)] = value;
    }

    [[nodiscard]] bool empty() const noexcept
    {
        for (bool value : selected)
        {
            if (value)
            {
                return false;
            }
        }
        return true;
    }

    friend bool operator==(
        const RaidIntelligenceLoadout &,
        const RaidIntelligenceLoadout &) = default;
};

struct RaidIntelligenceArchiveState
{
    std::map<
        MapDefinitionId,
        std::array<std::uint32_t, kRaidIntelligenceCategoryCount>> counts;

    [[nodiscard]] std::uint32_t count(
        const MapDefinitionId &mapId,
        RaidIntelligenceCategory category) const noexcept
    {
        const auto found = counts.find(mapId);
        return found == counts.end()
            ? 0U
            : found->second[raidIntelligenceCategoryIndex(category)];
    }

    friend bool operator==(
        const RaidIntelligenceArchiveState &,
        const RaidIntelligenceArchiveState &) = default;
};
