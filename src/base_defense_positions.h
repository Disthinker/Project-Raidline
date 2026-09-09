#pragma once
#include "base_fortification_types.h"
#include "home_region_layout.h"
#include <array>
#include <span>

struct BaseDefensePosition
{
    DefenseSlotKey key;
    ContentRect footprint;
    ContentRect circulation; // reserved local player/enemy passage around the solid barrier
    Vec2 outsideApproach;
    Vec2 insideApproach;
    bool available{};
};

// Positions depend only on the frozen site/plot geometry and definition. A
// dynamic crate can disable a position, but cannot reroll its fixed identity.
[[nodiscard]] std::array<BaseDefensePosition, 4> baseDefensePositionCandidates(
    const HomeRegionLayout &, std::string_view plot, const FortificationDefinition &);
[[nodiscard]] bool baseDefensePositionClear(const BaseDefensePosition &,
                                            std::span<const ContentRect> dynamicBlockers) noexcept;
