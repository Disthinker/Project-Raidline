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
    if (worldSize.x >= 10000.0F || worldSize.y >= 6000.0F)
    {
        columns_ = 80;
        rows_ = 45;
    }
    else
    {
        columns_ = 32;
        rows_ = 18;
    }
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
    const float revealRadius = worldSize_.x >= 10000.0F ? 480.0F : 155.0F;
    const float discoveryRadius = worldSize_.x >= 10000.0F ? 420.0F : 145.0F;
    const float cellWidth = worldSize_.x / static_cast<float>(columns_);
    const float cellHeight = worldSize_.y / static_cast<float>(rows_);
    const int firstColumn = std::clamp(
        static_cast<int>(std::floor(
            (worldPosition.x - revealRadius) / cellWidth)),
        0, columns_ - 1);
    const int lastColumn = std::clamp(
        static_cast<int>(std::floor(
            (worldPosition.x + revealRadius) / cellWidth)),
        0, columns_ - 1);
    const int firstRow = std::clamp(
        static_cast<int>(std::floor(
            (worldPosition.y - revealRadius) / cellHeight)),
        0, rows_ - 1);
    const int lastRow = std::clamp(
        static_cast<int>(std::floor(
            (worldPosition.y + revealRadius) / cellHeight)),
        0, rows_ - 1);
    for (int row = firstRow; row <= lastRow; ++row)
    {
        for (int column = firstColumn; column <= lastColumn; ++column)
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

void RaidTacticalMapState::configureOutdoorLayout(
    const RaidGeneratedMapLayout &layout,
    std::uint32_t sourceColumns,
    std::uint32_t sourceRows)
{
    outdoorRoadCells_.clear();
    outdoorDistrictKinds_.clear();
    outdoorTerrainKinds_.clear();
    outdoorLabels_.clear();
    if (!configured() || layout.layoutVersion < 3U || sourceColumns == 0U ||
        sourceRows == 0U)
    {
        return;
    }
    std::vector<std::optional<RaidOutdoorRoadKind>> cells(
        static_cast<std::size_t>(columns_ * rows_));
    for (const RaidOutdoorRoadCell &road : layout.roadCells)
    {
        const int column = std::clamp(
            static_cast<int>((static_cast<std::uint64_t>(road.column) *
                              static_cast<std::uint64_t>(columns_)) /
                             sourceColumns),
            0, columns_ - 1);
        const int row = std::clamp(
            static_cast<int>((static_cast<std::uint64_t>(road.row) *
                              static_cast<std::uint64_t>(rows_)) /
                             sourceRows),
            0, rows_ - 1);
        auto &stored = cells[static_cast<std::size_t>(row * columns_ + column)];
        if (!stored.has_value() ||
            static_cast<std::uint8_t>(road.kind) >
                static_cast<std::uint8_t>(*stored))
        {
            stored = road.kind;
        }
    }
    for (int row{}; row < rows_; ++row)
        for (int column{}; column < columns_; ++column)
        {
            const auto &kind =
                cells[static_cast<std::size_t>(row * columns_ + column)];
            if (kind.has_value())
                outdoorRoadCells_.push_back({column, row, *kind});
        }

    std::vector<std::optional<RaidTerrainKind>> sourceTerrainKinds(
        static_cast<std::size_t>(sourceColumns) * sourceRows);
    for (const RaidTerrainSpan &span : layout.terrainSpans)
    {
        if (span.row >= sourceRows || span.firstColumn >= sourceColumns)
        {
            continue;
        }
        const std::uint32_t endColumn = std::min<std::uint32_t>(
            sourceColumns,
            static_cast<std::uint32_t>(span.firstColumn) + span.length);
        for (std::uint32_t column = span.firstColumn;
             column < endColumn; ++column)
        {
            sourceTerrainKinds[
                static_cast<std::size_t>(span.row) * sourceColumns +
                column] = span.kind;
        }
    }
    outdoorTerrainKinds_.resize(
        static_cast<std::size_t>(columns_ * rows_));
    for (int row{}; row < rows_; ++row)
        for (int column{}; column < columns_; ++column)
        {
            const std::uint32_t sourceColumn = std::min(
                sourceColumns - 1U,
                static_cast<std::uint32_t>(column) * sourceColumns /
                    static_cast<std::uint32_t>(columns_));
            const std::uint32_t sourceRow = std::min(
                sourceRows - 1U,
                static_cast<std::uint32_t>(row) * sourceRows /
                    static_cast<std::uint32_t>(rows_));
            outdoorTerrainKinds_[
                static_cast<std::size_t>(row * columns_ + column)] =
                sourceTerrainKinds[
                    static_cast<std::size_t>(sourceRow) * sourceColumns +
                    sourceColumn];
        }

    std::uint32_t districtColumns{};
    std::uint32_t districtRows{};
    for (const RaidDistrictSnapshot &district : layout.districts)
        for (const RaidGridSpan &span : district.cells)
        {
            districtColumns = std::max<std::uint32_t>(
                districtColumns, span.firstColumn + span.length);
            districtRows = std::max<std::uint32_t>(
                districtRows, span.row + 1U);
        }
    if (districtColumns > 0U && districtRows > 0U)
    {
        std::vector<std::optional<RaidDistrictKind>> sourceDistrictKinds(
            static_cast<std::size_t>(districtColumns) * districtRows);
        for (const RaidDistrictSnapshot &district : layout.districts)
            for (const RaidGridSpan &span : district.cells)
                for (std::uint32_t column = span.firstColumn;
                     column < span.firstColumn + span.length; ++column)
                    sourceDistrictKinds[
                        static_cast<std::size_t>(span.row) *
                            districtColumns + column] = district.kind;
        outdoorDistrictKinds_.resize(
            static_cast<std::size_t>(columns_ * rows_));
        for (int row{}; row < rows_; ++row)
            for (int column{}; column < columns_; ++column)
            {
                const std::uint32_t sourceColumn = std::min(
                    districtColumns - 1U,
                    static_cast<std::uint32_t>(column) * districtColumns /
                        static_cast<std::uint32_t>(columns_));
                const std::uint32_t sourceRow = std::min(
                    districtRows - 1U,
                    static_cast<std::uint32_t>(row) * districtRows /
                        static_cast<std::uint32_t>(rows_));
                outdoorDistrictKinds_[
                    static_cast<std::size_t>(row * columns_ + column)] =
                    sourceDistrictKinds[
                        static_cast<std::size_t>(sourceRow) *
                            districtColumns + sourceColumn];
            }
    }
    for (const RaidDistrictSnapshot &district : layout.districts)
        outdoorLabels_.push_back(
            {district.displayName, district.labelPosition, false});
    for (const RaidLandmarkPlacementSnapshot &landmark : layout.landmarks)
        outdoorLabels_.push_back({
            landmark.displayName,
            {landmark.bounds.position.x + landmark.bounds.size.x * 0.5F,
             landmark.bounds.position.y + landmark.bounds.size.y * 0.5F},
            true});
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

const std::vector<RaidTacticalRoadCell> &
RaidTacticalMapState::outdoorRoadCells() const noexcept
{
    return outdoorRoadCells_;
}

std::optional<RaidDistrictKind> RaidTacticalMapState::outdoorDistrictKind(
    int column,
    int row) const noexcept
{
    if (column < 0 || row < 0 || column >= columns_ || row >= rows_ ||
        outdoorDistrictKinds_.size() !=
            static_cast<std::size_t>(columns_ * rows_))
    {
        return std::nullopt;
    }
    return outdoorDistrictKinds_[
        static_cast<std::size_t>(row * columns_ + column)];
}

std::optional<RaidTerrainKind> RaidTacticalMapState::outdoorTerrainKind(
    int column,
    int row) const noexcept
{
    if (column < 0 || row < 0 || column >= columns_ || row >= rows_ ||
        outdoorTerrainKinds_.size() !=
            static_cast<std::size_t>(columns_ * rows_))
    {
        return std::nullopt;
    }
    return outdoorTerrainKinds_[
        static_cast<std::size_t>(row * columns_ + column)];
}

const std::vector<RaidTacticalWorldLabel> &
RaidTacticalMapState::outdoorLabels() const noexcept
{
    return outdoorLabels_;
}
