#include "raid_tactical_map_presentation.h"

#include <algorithm>
#include <cmath>

Vec2 RaidTacticalMapChart::screenPoint(Vec2 point) const noexcept
{
    return {
        kRaidTacticalChart.position.x + point.x / worldSize.x * kRaidTacticalChart.size.x,
        kRaidTacticalChart.position.y + point.y / worldSize.y * kRaidTacticalChart.size.y};
}

std::optional<Vec2> RaidTacticalMapChart::worldPoint(Vec2 point) const noexcept
{
    const float x = point.x - kRaidTacticalChart.position.x;
    const float y = point.y - kRaidTacticalChart.position.y;
    if (!(x >= 0.0F && x < kRaidTacticalChart.size.x &&
          y >= 0.0F && y < kRaidTacticalChart.size.y &&
          worldSize.x > 0.0F && worldSize.y > 0.0F))
        return std::nullopt;
    return Vec2{x / kRaidTacticalChart.size.x * worldSize.x,
                y / kRaidTacticalChart.size.y * worldSize.y};
}

void RaidChalkRoutes::setSpace(std::optional<RaidChalkSpace> space) noexcept
{
    if (space_ != space)
        endStroke();
    space_ = space;
}

void RaidChalkRoutes::beginStroke(Vec2 point) noexcept
{
    endStroke();
    if (space_ && remainingLength() > 0.0)
        lastPoint_ = point;
}

void RaidChalkRoutes::extendStroke(Vec2 point)
{
    if (!lastPoint_)
        return;

    const double dx = static_cast<double>(point.x) - lastPoint_->x;
    const double dy = static_cast<double>(point.y) - lastPoint_->y;
    const double distance = std::hypot(dx, dy);
    // Sampling at eight world units also bounds the number of stored points.
    if (!std::isfinite(distance) || distance < kMinimumSegmentLength)
        return;

    const double available = remainingLength();
    const double length = std::min(distance, available);
    const double ratio = length / distance;
    const Vec2 end{static_cast<float>(lastPoint_->x + dx * ratio),
                   static_cast<float>(lastPoint_->y + dy * ratio)};

    auto &route = strokes_[static_cast<std::size_t>(*space_)];
    if (!hasSegment_)
    {
        route.push_back({{*lastPoint_}, 0.0});
        hasSegment_ = true;
    }
    route.back().points.push_back(end);
    route.back().length += length;
    usedLength_ += length;
    lastPoint_ = end;
    if (length == available)
        endStroke();
}

void RaidChalkRoutes::endStroke() noexcept
{
    lastPoint_.reset();
    hasSegment_ = false;
}

void RaidChalkRoutes::undo(RaidChalkSpace space) noexcept
{
    endStroke();
    auto &route = strokes_[static_cast<std::size_t>(space)];
    if (!route.empty())
    {
        usedLength_ = std::max(0.0, usedLength_ - route.back().length);
        route.pop_back();
    }
}

void RaidChalkRoutes::clear(RaidChalkSpace space) noexcept
{
    endStroke();
    auto &route = strokes_[static_cast<std::size_t>(space)];
    for (const auto &stroke : route)
        usedLength_ = std::max(0.0, usedLength_ - stroke.length);
    route.clear();
}

std::span<const RaidChalkStroke> RaidChalkRoutes::strokes(RaidChalkSpace space) const noexcept
{
    return strokes_[static_cast<std::size_t>(space)];
}

double RaidChalkRoutes::remainingLength() const noexcept
{
    return std::max(0.0, kLengthLimit - usedLength_);
}

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

bool tacticalMapObjectiveVisible(
    const RaidTacticalMapState &map,
    const RaidTacticalObjective &objective,
    RaidTacticalMapPresentationMode mode) noexcept
{
    const Vec2 center{
        objective.bounds.position.x + objective.bounds.size.x * 0.5F,
        objective.bounds.position.y + objective.bounds.size.y * 0.5F};
    return mode == RaidTacticalMapPresentationMode::FullStaticMap ||
        objective.visibility == RaidTacticalObjectiveVisibility::Briefed ||
        map.pointRevealed(center);
}

bool tacticalMapEnemyDeploymentVisible(
    const RaidTacticalMapState &map) noexcept
{
    return map.hasIntelligence(RaidIntelligenceCategory::Enemy);
}
