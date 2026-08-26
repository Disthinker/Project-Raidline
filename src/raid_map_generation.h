#pragma once

#include <cstdint>
#include <vector>

#include "content_registry.h"

struct RaidMapGenerationAnchors
{
    Vec2 playerSpawn{};
    ContentRect extractionPoint;
    std::vector<ContentRect> occupiedRegions;
    std::vector<Vec2> reachablePoints;
};

struct RaidGeneratedMapLayout
{
    std::vector<ContentRect> ballisticBlockers;
    std::uint32_t generationAttempt{};
    std::uint64_t layoutHash{};
    bool usedFallback{};

    friend bool operator==(
        const RaidGeneratedMapLayout &,
        const RaidGeneratedMapLayout &) = default;
};

[[nodiscard]] RaidGeneratedMapLayout generateRaidMapLayout(
    const MapDefinition &map,
    std::uint64_t raidSeed,
    const RaidMapGenerationAnchors &anchors);

[[nodiscard]] bool raidMapLayoutConnectsAnchors(
    const MapDefinition &map,
    const RaidGeneratedMapLayout &layout,
    const RaidMapGenerationAnchors &anchors) noexcept;

[[nodiscard]] std::uint64_t raidMapLayoutHash(
    const std::vector<ContentRect> &ballisticBlockers) noexcept;
