#include "raid_tactical_map.h"

#include <algorithm>
#include <cmath>
#include <set>
#include <stdexcept>
#include <utility>

namespace
{
Vec2 centerOf(ContentRect rect) noexcept
{
    return {rect.position.x + rect.size.x * 0.5F,
            rect.position.y + rect.size.y * 0.5F};
}

float distanceSquared(Vec2 first, Vec2 second) noexcept
{
    const float x = first.x - second.x;
    const float y = first.y - second.y;
    return x * x + y * y;
}
}

void RaidTacticalMapState::configure(
    Vec2 worldSize,
    RaidIntelligenceLoadout intelligence,
    ContentRect normalExtraction,
    std::optional<ContentRect> emergencyExtraction,
    std::optional<ContentRect> conditionalExtraction,
    std::optional<ContentRect> advancedResourceArea,
    std::vector<Vec2> initialEnemyCenters,
    std::vector<RaidSpecialLocationMapState> specialLocations)
{
    if (!std::isfinite(worldSize.x) || !std::isfinite(worldSize.y) ||
        worldSize.x <= 0.0F || worldSize.y <= 0.0F)
    {
        throw std::invalid_argument{"tactical map world size is invalid"};
    }
    std::set<RaidSpaceDefinitionId> specialLocationIds;
    for (RaidSpecialLocationMapState &location : specialLocations)
    {
        const ContentRect entrance = location.entrance;
        if (location.id == outdoorRaidSpaceId() ||
            location.displayName.empty() ||
            !specialLocationIds.insert(location.id).second ||
            !std::isfinite(entrance.position.x) ||
            !std::isfinite(entrance.position.y) ||
            !std::isfinite(entrance.size.x) ||
            !std::isfinite(entrance.size.y) ||
            entrance.position.x < 0.0F ||
            entrance.position.y < 0.0F ||
            entrance.size.x <= 0.0F || entrance.size.y <= 0.0F ||
            entrance.position.x + entrance.size.x > worldSize.x ||
            entrance.position.y + entrance.size.y > worldSize.y)
        {
            throw std::invalid_argument{
                "tactical map special location is invalid"};
        }
        location.discovered = false;
    }
    worldSize_ = worldSize;
    intelligence_ = intelligence;
    normalExtraction_ = normalExtraction;
    emergencyExtraction_ = emergencyExtraction;
    conditionalExtraction_ = conditionalExtraction;
    advancedResourceArea_ = advancedResourceArea;
    initialEnemyCenters_ = std::move(initialEnemyCenters);
    specialLocations_ = std::move(specialLocations);
    revealed_.assign(static_cast<std::size_t>(columns_ * rows_), false);
    normalExtractionDiscovered_ = false;
    emergencyExtractionDiscovered_ = false;
    conditionalExtractionDiscovered_ = false;
}

void RaidTacticalMapState::revealAround(Vec2 worldPosition) noexcept
{
    if (!configured())
    {
        return;
    }
    constexpr float revealRadius{155.0F};
    constexpr float discoveryRadius{145.0F};
    const float cellWidth = worldSize_.x / static_cast<float>(columns_);
    const float cellHeight = worldSize_.y / static_cast<float>(rows_);
    for (int row = 0; row < rows_; ++row)
    {
        for (int column = 0; column < columns_; ++column)
        {
            const Vec2 center{
                (static_cast<float>(column) + 0.5F) * cellWidth,
                (static_cast<float>(row) + 0.5F) * cellHeight};
            if (distanceSquared(center, worldPosition) <=
                revealRadius * revealRadius)
            {
                revealed_[static_cast<std::size_t>(row * columns_ + column)] =
                    true;
            }
        }
    }

    const auto discovered = [worldPosition, discoveryRadius](
        const std::optional<ContentRect> &rect) noexcept
    {
        return rect.has_value() &&
            distanceSquared(centerOf(*rect), worldPosition) <=
                discoveryRadius * discoveryRadius;
    };
    normalExtractionDiscovered_ = normalExtractionDiscovered_ ||
        distanceSquared(centerOf(normalExtraction_), worldPosition) <=
            discoveryRadius * discoveryRadius;
    emergencyExtractionDiscovered_ = emergencyExtractionDiscovered_ ||
        discovered(emergencyExtraction_);
    conditionalExtractionDiscovered_ = conditionalExtractionDiscovered_ ||
        discovered(conditionalExtraction_);
    for (RaidSpecialLocationMapState &location : specialLocations_)
    {
        location.discovered = location.discovered ||
            distanceSquared(centerOf(location.entrance), worldPosition) <=
                discoveryRadius * discoveryRadius;
    }
}

bool RaidTacticalMapState::configured() const noexcept
{
    return !revealed_.empty();
}
Vec2 RaidTacticalMapState::worldSize() const noexcept { return worldSize_; }
int RaidTacticalMapState::columns() const noexcept { return columns_; }
int RaidTacticalMapState::rows() const noexcept { return rows_; }

bool RaidTacticalMapState::cellRevealed(int column, int row) const noexcept
{
    return column >= 0 && row >= 0 && column < columns_ && row < rows_ &&
        revealed_[static_cast<std::size_t>(row * columns_ + column)];
}

bool RaidTacticalMapState::pointRevealed(Vec2 point) const noexcept
{
    if (!configured() || point.x < 0.0F || point.y < 0.0F ||
        point.x >= worldSize_.x || point.y >= worldSize_.y)
    {
        return false;
    }
    const int column = std::clamp(
        static_cast<int>(point.x / worldSize_.x * columns_), 0, columns_ - 1);
    const int row = std::clamp(
        static_cast<int>(point.y / worldSize_.y * rows_), 0, rows_ - 1);
    return cellRevealed(column, row);
}

bool RaidTacticalMapState::extractionVisible(
    RaidMapExtractionKind kind) const noexcept
{
    if (hasIntelligence(RaidIntelligenceCategory::Transport))
    {
        return true;
    }
    switch (kind)
    {
    case RaidMapExtractionKind::Normal:
        return normalExtractionDiscovered_;
    case RaidMapExtractionKind::EmergencySignal:
        return emergencyExtractionDiscovered_;
    case RaidMapExtractionKind::EmergencyConditional:
        return conditionalExtractionDiscovered_;
    }
    return false;
}

bool RaidTacticalMapState::hasIntelligence(
    RaidIntelligenceCategory category) const noexcept
{
    return intelligence_.has(category);
}
const ContentRect &RaidTacticalMapState::normalExtraction() const noexcept
{
    return normalExtraction_;
}
const std::optional<ContentRect> &
RaidTacticalMapState::emergencyExtraction() const noexcept
{
    return emergencyExtraction_;
}
const std::optional<ContentRect> &
RaidTacticalMapState::conditionalExtraction() const noexcept
{
    return conditionalExtraction_;
}
const std::optional<ContentRect> &
RaidTacticalMapState::advancedResourceArea() const noexcept
{
    return advancedResourceArea_;
}
const std::vector<Vec2> &
RaidTacticalMapState::initialEnemyCenters() const noexcept
{
    return initialEnemyCenters_;
}

const std::vector<RaidSpecialLocationMapState> &
RaidTacticalMapState::specialLocations() const noexcept
{
    return specialLocations_;
}

bool RaidTacticalMapState::specialLocationVisible(
    const RaidSpaceDefinitionId &id) const noexcept
{
    const auto found = std::find_if(
        specialLocations_.begin(), specialLocations_.end(),
        [&](const RaidSpecialLocationMapState &location)
        { return location.id == id; });
    return found != specialLocations_.end() && found->discovered;
}
