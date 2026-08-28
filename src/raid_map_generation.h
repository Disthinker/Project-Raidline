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

enum class RaidOutdoorRoadKind : std::uint8_t
{
    Access,
    Secondary,
    Primary
};

struct RaidOutdoorRoadCell
{
    std::uint16_t column{};
    std::uint16_t row{};
    RaidOutdoorRoadKind kind{RaidOutdoorRoadKind::Access};

    friend bool operator==(
        const RaidOutdoorRoadCell &,
        const RaidOutdoorRoadCell &) = default;
};

enum class RaidMapFallbackReason : std::uint8_t
{
    None,
    AttemptsExhausted
};

struct RaidGeneratedMapLayout
{
    std::uint32_t layoutVersion{};
    std::vector<RaidOutdoorRoadCell> roadCells;
    std::vector<ContentRect> ballisticBlockers;
    std::uint32_t generationAttempt{};
    std::uint64_t layoutHash{};
    bool usedFallback{};
    RaidMapFallbackReason fallbackReason{RaidMapFallbackReason::None};

    friend bool operator==(
        const RaidGeneratedMapLayout &,
        const RaidGeneratedMapLayout &) = default;
};

[[nodiscard]] bool raidExteriorPlacementIsLegal(
    const RaidExteriorPlacementDefinition &placement,
    const RaidMapGenerationAnchors &anchors) noexcept;

[[nodiscard]] const RaidExteriorPlacementDefinition *
selectRaidExteriorPlacement(
    const RaidInteriorDefinition &interior,
    std::uint64_t raidSeed,
    std::uint64_t interiorOrdinal,
    const RaidMapGenerationAnchors &anchors) noexcept;

void appendRaidExteriorPlacementAnchors(
    RaidMapGenerationAnchors &anchors,
    const RaidExteriorPlacementDefinition &placement);

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

[[nodiscard]] std::uint64_t raidMapLayoutHash(
    const RaidGeneratedMapLayout &layout) noexcept;
