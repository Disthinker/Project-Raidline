#include "raid_tactical_map_presentation.h"

bool tacticalMapCellVisible(
    const RaidTacticalMapState &map,
    int column,
    int row,
    RaidTacticalMapPresentationMode mode) noexcept
{
    const bool inBounds = column >= 0 && row >= 0 &&
        column < map.columns() && row < map.rows();
    return inBounds &&
        (mode == RaidTacticalMapPresentationMode::FullStaticMap ||
         map.cellRevealed(column, row));
}

bool tacticalMapPointVisible(
    const RaidTacticalMapState &map,
    Vec2 point,
    RaidTacticalMapPresentationMode mode) noexcept
{
    const Vec2 size = map.worldSize();
    const bool inBounds = point.x >= 0.0F && point.y >= 0.0F &&
        point.x < size.x && point.y < size.y;
    return inBounds &&
        (mode == RaidTacticalMapPresentationMode::FullStaticMap ||
         map.pointRevealed(point));
}

bool tacticalMapExtractionVisible(
    const RaidTacticalMapState &map,
    RaidMapExtractionKind kind,
    RaidTacticalMapPresentationMode mode) noexcept
{
    return mode == RaidTacticalMapPresentationMode::FullStaticMap ||
        map.extractionVisible(kind);
}

bool tacticalMapAdvancedResourceVisible(
    const RaidTacticalMapState &map,
    RaidTacticalMapPresentationMode mode) noexcept
{
    return map.advancedResourceArea().has_value() &&
        (mode == RaidTacticalMapPresentationMode::FullStaticMap ||
         map.hasIntelligence(RaidIntelligenceCategory::Resource));
}

bool tacticalMapResourcePointVisible(
    const RaidTacticalMapState &map,
    const RaidTacticalResourcePoint &resourcePoint,
    RaidTacticalMapPresentationMode mode) noexcept
{
    const Vec2 center{
        resourcePoint.bounds.position.x +
            resourcePoint.bounds.size.x * 0.5F,
        resourcePoint.bounds.position.y +
            resourcePoint.bounds.size.y * 0.5F};
    return mode == RaidTacticalMapPresentationMode::FullStaticMap ||
        map.hasIntelligence(RaidIntelligenceCategory::Resource) ||
        map.pointRevealed(center);
}

bool tacticalMapSpecialLocationVisible(
    const RaidSpecialLocationMapState &location,
    RaidTacticalMapPresentationMode mode) noexcept
{
    return mode == RaidTacticalMapPresentationMode::FullStaticMap ||
        location.discovered;
}

bool tacticalMapEnemyDeploymentVisible(
    const RaidTacticalMapState &map) noexcept
{
    return map.hasIntelligence(RaidIntelligenceCategory::Enemy);
}
