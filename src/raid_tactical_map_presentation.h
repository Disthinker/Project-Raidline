#pragma once

#include "raid_tactical_map.h"

enum class RaidTacticalMapPresentationMode
{
    FogOfWar,
    FullStaticMap
};

[[nodiscard]] bool tacticalMapCellVisible(
    const RaidTacticalMapState &map,
    int column,
    int row,
    RaidTacticalMapPresentationMode mode) noexcept;

[[nodiscard]] bool tacticalMapPointVisible(
    const RaidTacticalMapState &map,
    Vec2 point,
    RaidTacticalMapPresentationMode mode) noexcept;

[[nodiscard]] bool tacticalMapExtractionVisible(
    const RaidTacticalMapState &map,
    RaidMapExtractionKind kind,
    RaidTacticalMapPresentationMode mode) noexcept;

[[nodiscard]] bool tacticalMapAdvancedResourceVisible(
    const RaidTacticalMapState &map,
    RaidTacticalMapPresentationMode mode) noexcept;

[[nodiscard]] bool tacticalMapSpecialLocationVisible(
    const RaidSpecialLocationMapState &location,
    RaidTacticalMapPresentationMode mode) noexcept;

// The full-static-map debug mode never grants or emulates enemy intelligence.
[[nodiscard]] bool tacticalMapEnemyDeploymentVisible(
    const RaidTacticalMapState &map) noexcept;
