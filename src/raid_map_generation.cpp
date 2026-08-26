#include "raid_map_generation.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <queue>
#include <stdexcept>
#include <utility>

#include "stable_random.h"

namespace
{
struct Cell
{
    std::uint32_t column{};
    std::uint32_t row{};
};

bool finitePositive(Vec2 value) noexcept
{
    return std::isfinite(value.x) && std::isfinite(value.y) &&
        value.x > 0.0F && value.y > 0.0F;
}

bool overlaps(ContentRect first, ContentRect second) noexcept
{
    return first.position.x < second.position.x + second.size.x &&
        first.position.x + first.size.x > second.position.x &&
        first.position.y < second.position.y + second.size.y &&
        first.position.y + first.size.y > second.position.y;
}

ContentRect inflated(ContentRect rect, float amount) noexcept
{
    return {{rect.position.x - amount, rect.position.y - amount},
            {rect.size.x + amount * 2.0F, rect.size.y + amount * 2.0F}};
}

ContentRect pointRegion(Vec2 point, float radius) noexcept
{
    return {{point.x - radius, point.y - radius},
            {radius * 2.0F, radius * 2.0F}};
}

std::uint64_t calculateLayoutHash(
    const std::vector<ContentRect> &blockers) noexcept
{
    constexpr std::uint64_t offset{1469598103934665603ULL};
    constexpr std::uint64_t prime{1099511628211ULL};
    std::uint64_t hash = offset;
    const auto add = [&hash](std::uint32_t value) noexcept
    {
        for (int byte = 0; byte < 4; ++byte)
        {
            hash ^= static_cast<std::uint8_t>(value >> (byte * 8));
            hash *= prime;
        }
    };
    for (const ContentRect &blocker : blockers)
    {
        add(static_cast<std::uint32_t>(std::lround(blocker.position.x * 10.0F)));
        add(static_cast<std::uint32_t>(std::lround(blocker.position.y * 10.0F)));
        add(static_cast<std::uint32_t>(std::lround(blocker.size.x * 10.0F)));
        add(static_cast<std::uint32_t>(std::lround(blocker.size.y * 10.0F)));
    }
    return hash;
}

std::vector<ContentRect> fallbackBlockers(const MapDefinition &map)
{
    std::vector<ContentRect> result;
    result.reserve(map.ballisticBlockers.size());
    for (const BallisticBlockerDefinition &blocker : map.ballisticBlockers)
    {
        result.push_back(blocker.bounds);
    }
    return result;
}

std::optional<Cell> cellForPoint(
    const MapDefinition &map,
    const ProceduralOutdoorDefinition &definition,
    Vec2 point) noexcept
{
    const Vec2 origin = map.walkableBounds.position;
    const float width = map.walkableBounds.size.x /
        static_cast<float>(definition.columns);
    const float height = map.walkableBounds.size.y /
        static_cast<float>(definition.rows);
    if (point.x < origin.x || point.y < origin.y ||
        point.x >= origin.x + map.walkableBounds.size.x ||
        point.y >= origin.y + map.walkableBounds.size.y)
    {
        return std::nullopt;
    }
    return Cell{
        std::min(definition.columns - 1U,
                 static_cast<std::uint32_t>((point.x - origin.x) / width)),
        std::min(definition.rows - 1U,
                 static_cast<std::uint32_t>((point.y - origin.y) / height))};
}

bool layoutConnects(
    const MapDefinition &map,
    const RaidGeneratedMapLayout &layout,
    const RaidMapGenerationAnchors &anchors) noexcept
{
    const ProceduralOutdoorDefinition &definition = map.proceduralOutdoor;
    if (definition.columns == 0U || definition.rows == 0U)
    {
        return false;
    }
    const std::size_t cellCount = static_cast<std::size_t>(
        definition.columns) * definition.rows;
    std::vector<bool> blocked(cellCount, false);
    const float width = map.walkableBounds.size.x /
        static_cast<float>(definition.columns);
    const float height = map.walkableBounds.size.y /
        static_cast<float>(definition.rows);
    for (std::uint32_t row = 0; row < definition.rows; ++row)
    {
        for (std::uint32_t column = 0; column < definition.columns; ++column)
        {
            const ContentRect cell{
                {map.walkableBounds.position.x + column * width,
                 map.walkableBounds.position.y + row * height},
                {width, height}};
            blocked[static_cast<std::size_t>(row) * definition.columns + column] =
                std::any_of(layout.ballisticBlockers.begin(),
                            layout.ballisticBlockers.end(),
                            [cell](ContentRect blocker)
                            { return overlaps(cell, blocker); });
        }
    }

    const auto start = cellForPoint(map, definition, anchors.playerSpawn);
    if (!start.has_value())
    {
        return false;
    }
    const std::size_t startIndex = static_cast<std::size_t>(start->row) *
        definition.columns + start->column;
    if (blocked[startIndex])
    {
        return false;
    }
    std::vector<bool> visited(cellCount, false);
    std::queue<Cell> open;
    visited[startIndex] = true;
    open.push(*start);
    constexpr int offsets[4][2]{{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
    while (!open.empty())
    {
        const Cell current = open.front();
        open.pop();
        for (const auto &offset : offsets)
        {
            const int column = static_cast<int>(current.column) + offset[0];
            const int row = static_cast<int>(current.row) + offset[1];
            if (column < 0 || row < 0 ||
                column >= static_cast<int>(definition.columns) ||
                row >= static_cast<int>(definition.rows))
            {
                continue;
            }
            const std::size_t index = static_cast<std::size_t>(row) *
                definition.columns + static_cast<std::size_t>(column);
            if (!blocked[index] && !visited[index])
            {
                visited[index] = true;
                open.push(Cell{static_cast<std::uint32_t>(column),
                               static_cast<std::uint32_t>(row)});
            }
        }
    }

    std::vector<Vec2> required = anchors.reachablePoints;
    required.push_back({
        anchors.extractionPoint.position.x + anchors.extractionPoint.size.x * 0.5F,
        anchors.extractionPoint.position.y + anchors.extractionPoint.size.y * 0.5F});
    return std::all_of(required.begin(), required.end(), [&](Vec2 point)
    {
        const auto cell = cellForPoint(map, definition, point);
        return cell.has_value() &&
            visited[static_cast<std::size_t>(cell->row) * definition.columns +
                    cell->column];
    });
}
}

bool raidExteriorPlacementIsLegal(
    const RaidExteriorPlacementDefinition &placement,
    const RaidMapGenerationAnchors &anchors) noexcept
{
    const ContentRect returnRegion = pointRegion(placement.returnPoint, 20.0F);
    const auto conflicts = [&](ContentRect reserved) noexcept
    {
        return overlaps(placement.entrance, reserved) ||
            overlaps(returnRegion, reserved);
    };
    if (conflicts(anchors.extractionPoint) ||
        conflicts(pointRegion(anchors.playerSpawn, 70.0F)) ||
        std::any_of(
            anchors.occupiedRegions.begin(),
            anchors.occupiedRegions.end(),
            conflicts))
    {
        return false;
    }
    return std::none_of(
        anchors.reachablePoints.begin(),
        anchors.reachablePoints.end(),
        [&](Vec2 point)
        { return conflicts(pointRegion(point, 35.0F)); });
}

const RaidExteriorPlacementDefinition *selectRaidExteriorPlacement(
    const RaidInteriorDefinition &interior,
    std::uint64_t raidSeed,
    std::uint64_t interiorOrdinal,
    const RaidMapGenerationAnchors &anchors) noexcept
{
    if (raidSeed == 0U || interior.exteriorPlacements.empty())
    {
        return nullptr;
    }
    std::vector<const RaidExteriorPlacementDefinition *> legal;
    legal.reserve(interior.exteriorPlacements.size());
    for (const RaidExteriorPlacementDefinition &placement :
         interior.exteriorPlacements)
    {
        if (raidExteriorPlacementIsLegal(placement, anchors))
        {
            legal.push_back(&placement);
        }
    }
    if (legal.empty())
    {
        return nullptr;
    }
    Pcg32 random{
        raidSeed,
        0x7370656369616c70ULL + interiorOrdinal};
    return legal[random.bounded(static_cast<std::uint32_t>(legal.size()))];
}

RaidGeneratedMapLayout generateRaidMapLayout(
    const MapDefinition &map,
    std::uint64_t raidSeed,
    const RaidMapGenerationAnchors &anchors)
{
    if (raidSeed == 0U || !finitePositive(map.walkableBounds.size))
    {
        throw std::invalid_argument{"Raid map generation input is invalid"};
    }
    if (!map.proceduralOutdoor.enabled)
    {
        RaidGeneratedMapLayout fixed;
        fixed.ballisticBlockers = fallbackBlockers(map);
        fixed.layoutHash = calculateLayoutHash(fixed.ballisticBlockers);
        return fixed;
    }

    const ProceduralOutdoorDefinition &definition = map.proceduralOutdoor;
    const float cellWidth = map.walkableBounds.size.x /
        static_cast<float>(definition.columns);
    const float cellHeight = map.walkableBounds.size.y /
        static_cast<float>(definition.rows);
    const float clearance = static_cast<float>(definition.anchorClearanceCells) *
        std::max(cellWidth, cellHeight);
    std::vector<ContentRect> reserved = anchors.occupiedRegions;
    reserved.push_back(inflated(anchors.extractionPoint, clearance));
    reserved.push_back(pointRegion(anchors.playerSpawn, clearance + 50.0F));
    for (Vec2 point : anchors.reachablePoints)
    {
        reserved.push_back(pointRegion(point, clearance + 35.0F));
    }

    std::vector<Cell> candidates;
    for (std::uint32_t row = 0; row < definition.rows; ++row)
    {
        for (std::uint32_t column = 0; column < definition.columns; ++column)
        {
            const ContentRect cell{
                {map.walkableBounds.position.x + column * cellWidth,
                 map.walkableBounds.position.y + row * cellHeight},
                {cellWidth, cellHeight}};
            if (std::none_of(reserved.begin(), reserved.end(),
                             [cell](ContentRect value)
                             { return overlaps(cell, value); }))
            {
                candidates.push_back(Cell{column, row});
            }
        }
    }

    for (std::uint32_t attempt = 1U;
         attempt <= definition.maximumAttempts;
         ++attempt)
    {
        Pcg32 random{raidSeed ^ (static_cast<std::uint64_t>(attempt) << 32U),
                     0x726169642d6d6170ULL};
        std::vector<Cell> shuffled = candidates;
        for (std::size_t index = shuffled.size(); index > 1U; --index)
        {
            const std::size_t selected = random.bounded(
                static_cast<std::uint32_t>(index));
            std::swap(shuffled[index - 1U], shuffled[selected]);
        }
        const std::uint32_t range =
            definition.maximumBlockers - definition.minimumBlockers + 1U;
        const std::size_t target = std::min<std::size_t>(
            definition.minimumBlockers + random.bounded(range),
            shuffled.size());
        RaidGeneratedMapLayout layout;
        layout.generationAttempt = attempt;
        layout.ballisticBlockers.reserve(target);
        constexpr float inset{5.0F};
        for (std::size_t index = 0; index < target; ++index)
        {
            const Cell cell = shuffled[index];
            layout.ballisticBlockers.push_back({
                {map.walkableBounds.position.x + cell.column * cellWidth + inset,
                 map.walkableBounds.position.y + cell.row * cellHeight + inset},
                {cellWidth - inset * 2.0F, cellHeight - inset * 2.0F}});
        }
        if (layoutConnects(map, layout, anchors))
        {
            layout.layoutHash = calculateLayoutHash(layout.ballisticBlockers);
            return layout;
        }
    }

    RaidGeneratedMapLayout fallback;
    fallback.ballisticBlockers = fallbackBlockers(map);
    fallback.generationAttempt = definition.maximumAttempts;
    fallback.layoutHash = calculateLayoutHash(fallback.ballisticBlockers);
    fallback.usedFallback = true;
    return fallback;
}

bool raidMapLayoutConnectsAnchors(
    const MapDefinition &map,
    const RaidGeneratedMapLayout &layout,
    const RaidMapGenerationAnchors &anchors) noexcept
{
    return layoutConnects(map, layout, anchors);
}

std::uint64_t raidMapLayoutHash(
    const std::vector<ContentRect> &ballisticBlockers) noexcept
{
    return calculateLayoutHash(ballisticBlockers);
}
