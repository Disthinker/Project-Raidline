#include "raid_map_generation.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>
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

struct RoadSkeleton
{
    std::vector<RaidOutdoorRoadCell> cells;
    std::vector<std::uint8_t> ranks;
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

void hashValue(std::uint64_t &hash, std::uint32_t value) noexcept
{
    constexpr std::uint64_t prime{1099511628211ULL};
    for (int byte = 0; byte < 4; ++byte)
    {
        hash ^= static_cast<std::uint8_t>(value >> (byte * 8));
        hash *= prime;
    }
}

std::uint8_t roadRank(RaidOutdoorRoadKind kind) noexcept
{
    return static_cast<std::uint8_t>(kind) + 1U;
}

std::optional<Cell> cellForPoint(
    const MapDefinition &map,
    const ProceduralOutdoorDefinition &definition,
    Vec2 point) noexcept;

void addRoadCell(
    RoadSkeleton &roads,
    const ProceduralOutdoorDefinition &definition,
    Cell cell,
    RaidOutdoorRoadKind kind)
{
    if (cell.column >= definition.columns || cell.row >= definition.rows)
    {
        return;
    }
    const std::size_t index = static_cast<std::size_t>(cell.row) *
        definition.columns + cell.column;
    const std::uint8_t rank = roadRank(kind);
    if (roads.ranks[index] >= rank)
    {
        return;
    }
    roads.ranks[index] = rank;
}

void connectToSpine(
    RoadSkeleton &roads,
    const ProceduralOutdoorDefinition &definition,
    Cell cell,
    std::uint32_t spineColumn,
    std::uint32_t spineRow)
{
    const std::uint32_t firstColumn = std::min(cell.column, spineColumn);
    const std::uint32_t lastColumn = std::max(cell.column, spineColumn);
    for (std::uint32_t column = firstColumn; column <= lastColumn; ++column)
    {
        addRoadCell(roads, definition, {column, cell.row},
                    RaidOutdoorRoadKind::Access);
    }
    const std::uint32_t firstRow = std::min(cell.row, spineRow);
    const std::uint32_t lastRow = std::max(cell.row, spineRow);
    for (std::uint32_t row = firstRow; row <= lastRow; ++row)
    {
        addRoadCell(roads, definition, {spineColumn, row},
                    RaidOutdoorRoadKind::Access);
    }
}

RoadSkeleton buildRoadSkeleton(
    const MapDefinition &map,
    const RaidMapGenerationAnchors &anchors,
    Pcg32 *random)
{
    const ProceduralOutdoorDefinition &definition = map.proceduralOutdoor;
    RoadSkeleton roads;
    roads.ranks.resize(
        static_cast<std::size_t>(definition.columns) * definition.rows, 0U);
    const std::uint32_t spineColumn = definition.columns / 3U +
        (random == nullptr ? definition.columns / 6U
                           : random->bounded(std::max(1U, definition.columns / 3U)));
    const std::uint32_t spineRow = definition.rows / 3U +
        (random == nullptr ? definition.rows / 6U
                           : random->bounded(std::max(1U, definition.rows / 3U)));

    for (std::uint32_t column{}; column < definition.columns; ++column)
    {
        addRoadCell(roads, definition, {column, spineRow},
                    RaidOutdoorRoadKind::Primary);
    }
    for (std::uint32_t row{}; row < definition.rows; ++row)
    {
        addRoadCell(roads, definition, {spineColumn, row},
                    RaidOutdoorRoadKind::Primary);
    }

    const std::uint32_t branchRange = definition.maximumBranchRoads -
        definition.minimumBranchRoads + 1U;
    const std::uint32_t branchCount = definition.minimumBranchRoads +
        (random == nullptr ? 0U : random->bounded(branchRange));
    std::vector<std::uint32_t> horizontalLines{spineRow};
    std::vector<std::uint32_t> verticalLines{spineColumn};
    for (std::uint32_t branch{}; branch < branchCount; ++branch)
    {
        const bool horizontal = branch % 2U == 0U;
        const std::uint32_t limit = horizontal ? definition.rows : definition.columns;
        std::uint32_t coordinate = random == nullptr
            ? ((branch + 1U) * limit) / (branchCount + 1U)
            : random->bounded(limit);
        auto &lines = horizontal ? horizontalLines : verticalLines;
        for (std::uint32_t probe{};
             probe < limit && std::find(lines.begin(), lines.end(), coordinate) != lines.end();
             ++probe)
        {
            coordinate = (coordinate + 1U) % limit;
        }
        if (std::find(lines.begin(), lines.end(), coordinate) != lines.end())
        {
            continue;
        }
        lines.push_back(coordinate);
        if (horizontal)
        {
            for (std::uint32_t column{}; column < definition.columns; ++column)
            {
                addRoadCell(roads, definition, {column, coordinate},
                            RaidOutdoorRoadKind::Secondary);
            }
        }
        else
        {
            for (std::uint32_t row{}; row < definition.rows; ++row)
            {
                addRoadCell(roads, definition, {coordinate, row},
                            RaidOutdoorRoadKind::Secondary);
            }
        }
    }

    std::vector<Vec2> required = anchors.reachablePoints;
    required.push_back(anchors.playerSpawn);
    required.push_back({
        anchors.extractionPoint.position.x + anchors.extractionPoint.size.x * 0.5F,
        anchors.extractionPoint.position.y + anchors.extractionPoint.size.y * 0.5F});
    for (Vec2 point : required)
    {
        if (const auto cell = cellForPoint(map, definition, point))
        {
            connectToSpine(roads, definition, *cell, spineColumn, spineRow);
        }
    }

    for (std::uint32_t row{}; row < definition.rows; ++row)
    {
        for (std::uint32_t column{}; column < definition.columns; ++column)
        {
            const std::uint8_t rank = roads.ranks[
                static_cast<std::size_t>(row) * definition.columns + column];
            if (rank > 0U)
            {
                roads.cells.push_back({
                    static_cast<std::uint16_t>(column),
                    static_cast<std::uint16_t>(row),
                    static_cast<RaidOutdoorRoadKind>(rank - 1U)});
            }
        }
    }
    return roads;
}

std::vector<ContentRect> generateBuildings(
    const MapDefinition &map,
    const RaidMapGenerationAnchors &anchors,
    const RoadSkeleton &roads,
    Pcg32 *random,
    std::uint32_t target)
{
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
    for (std::uint32_t row{}; row < definition.rows; ++row)
    {
        for (std::uint32_t column{}; column < definition.columns; ++column)
        {
            const std::size_t index = static_cast<std::size_t>(row) *
                definition.columns + column;
            if (roads.ranks[index] == 0U)
            {
                candidates.push_back({column, row});
            }
        }
    }
    if (random != nullptr)
    {
        for (std::size_t index = candidates.size(); index > 1U; --index)
        {
            const std::size_t selected = random->bounded(
                static_cast<std::uint32_t>(index));
            std::swap(candidates[index - 1U], candidates[selected]);
        }
    }

    std::vector<bool> occupied(roads.ranks.size(), false);
    std::vector<ContentRect> result;
    result.reserve(target);
    constexpr float inset{5.0F};
    for (Cell origin : candidates)
    {
        if (result.size() >= target)
        {
            break;
        }
        const std::uint32_t widthCells = random == nullptr
            ? 1U : 1U + (random->bounded(4U) == 0U ? 1U : 0U);
        const std::uint32_t heightCells = random == nullptr
            ? 1U : 1U + (random->bounded(4U) == 0U ? 1U : 0U);
        if (origin.column + widthCells > definition.columns ||
            origin.row + heightCells > definition.rows)
        {
            continue;
        }
        bool legal = true;
        for (std::uint32_t row = origin.row; row < origin.row + heightCells; ++row)
        {
            for (std::uint32_t column = origin.column;
                 column < origin.column + widthCells;
                 ++column)
            {
                const std::size_t index = static_cast<std::size_t>(row) *
                    definition.columns + column;
                legal = legal && roads.ranks[index] == 0U && !occupied[index];
            }
        }
        const ContentRect building{
            {map.walkableBounds.position.x + origin.column * cellWidth + inset,
             map.walkableBounds.position.y + origin.row * cellHeight + inset},
            {widthCells * cellWidth - inset * 2.0F,
             heightCells * cellHeight - inset * 2.0F}};
        legal = legal && std::none_of(
            reserved.begin(), reserved.end(),
            [building](ContentRect value) { return overlaps(building, value); });
        if (!legal)
        {
            continue;
        }
        result.push_back(building);
        for (std::uint32_t row = origin.row; row < origin.row + heightCells; ++row)
        {
            for (std::uint32_t column = origin.column;
                 column < origin.column + widthCells;
                 ++column)
            {
                occupied[static_cast<std::size_t>(row) * definition.columns +
                         column] = true;
            }
        }
    }
    return result;
}

bool roadLayoutIsValid(
    const MapDefinition &map,
    const RaidGeneratedMapLayout &layout,
    const RaidMapGenerationAnchors &anchors) noexcept
{
    const ProceduralOutdoorDefinition &definition = map.proceduralOutdoor;
    const std::size_t cellCount = static_cast<std::size_t>(definition.columns) *
        definition.rows;
    std::vector<bool> road(cellCount, false);
    bool hasPrimary = false;
    for (const RaidOutdoorRoadCell &cell : layout.roadCells)
    {
        if (cell.column >= definition.columns || cell.row >= definition.rows)
        {
            return false;
        }
        const std::size_t index = static_cast<std::size_t>(cell.row) *
            definition.columns + cell.column;
        if (road[index])
        {
            return false;
        }
        road[index] = true;
        hasPrimary = hasPrimary || cell.kind == RaidOutdoorRoadKind::Primary;
    }
    if (!hasPrimary || layout.roadCells.empty())
    {
        return false;
    }
    std::vector<Vec2> required = anchors.reachablePoints;
    required.push_back(anchors.playerSpawn);
    required.push_back({
        anchors.extractionPoint.position.x + anchors.extractionPoint.size.x * 0.5F,
        anchors.extractionPoint.position.y + anchors.extractionPoint.size.y * 0.5F});
    for (Vec2 point : required)
    {
        const auto cell = cellForPoint(map, definition, point);
        if (!cell.has_value() || !road[static_cast<std::size_t>(cell->row) *
                                      definition.columns + cell->column])
        {
            return false;
        }
    }
    const float cellWidth = map.walkableBounds.size.x /
        static_cast<float>(definition.columns);
    const float cellHeight = map.walkableBounds.size.y /
        static_cast<float>(definition.rows);
    for (const RaidOutdoorRoadCell &cell : layout.roadCells)
    {
        const ContentRect bounds{
            {map.walkableBounds.position.x + cell.column * cellWidth,
             map.walkableBounds.position.y + cell.row * cellHeight},
            {cellWidth, cellHeight}};
        if (std::any_of(layout.ballisticBlockers.begin(),
                        layout.ballisticBlockers.end(),
                        [bounds](ContentRect blocker)
                        { return overlaps(bounds, blocker); }))
        {
            return false;
        }
    }
    return true;
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

void appendRaidExteriorPlacementAnchors(
    RaidMapGenerationAnchors &anchors,
    const RaidExteriorPlacementDefinition &placement)
{
    anchors.occupiedRegions.push_back(placement.entrance);
    anchors.occupiedRegions.push_back(pointRegion(placement.returnPoint, 20.0F));
    anchors.reachablePoints.push_back(
        Vec2{placement.entrance.position.x + placement.entrance.size.x * 0.5F,
             placement.entrance.position.y + placement.entrance.size.y * 0.5F});
    anchors.reachablePoints.push_back(placement.returnPoint);
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
    for (std::uint32_t attempt = 1U;
         attempt <= definition.maximumAttempts;
         ++attempt)
    {
        Pcg32 random{raidSeed ^ (static_cast<std::uint64_t>(attempt) << 32U),
                     0x726169642d6d6170ULL};
        const std::uint32_t range =
            definition.maximumBlockers - definition.minimumBlockers + 1U;
        const std::uint32_t target = definition.minimumBlockers +
            random.bounded(range);
        const RoadSkeleton roads = buildRoadSkeleton(map, anchors, &random);
        RaidGeneratedMapLayout layout;
        layout.layoutVersion = definition.layoutVersion;
        layout.generationAttempt = attempt;
        layout.roadCells = roads.cells;
        layout.ballisticBlockers = generateBuildings(
            map, anchors, roads, &random, target);
        if (layout.ballisticBlockers.size() >= definition.minimumBlockers &&
            roadLayoutIsValid(map, layout, anchors) &&
            layoutConnects(map, layout, anchors))
        {
            layout.layoutHash = raidMapLayoutHash(layout);
            return layout;
        }
    }

    const RoadSkeleton roads = buildRoadSkeleton(map, anchors, nullptr);
    RaidGeneratedMapLayout fallback;
    fallback.layoutVersion = definition.layoutVersion;
    fallback.roadCells = roads.cells;
    fallback.ballisticBlockers = generateBuildings(
        map, anchors, roads, nullptr, definition.minimumBlockers);
    fallback.generationAttempt = definition.maximumAttempts;
    fallback.usedFallback = true;
    fallback.fallbackReason = RaidMapFallbackReason::AttemptsExhausted;
    fallback.layoutHash = raidMapLayoutHash(fallback);
    return fallback;
}

bool raidMapLayoutConnectsAnchors(
    const MapDefinition &map,
    const RaidGeneratedMapLayout &layout,
    const RaidMapGenerationAnchors &anchors) noexcept
{
    return (!map.proceduralOutdoor.enabled || layout.layoutVersion == 0U ||
            roadLayoutIsValid(map, layout, anchors)) &&
        layoutConnects(map, layout, anchors);
}

std::uint64_t raidMapLayoutHash(
    const std::vector<ContentRect> &ballisticBlockers) noexcept
{
    return calculateLayoutHash(ballisticBlockers);
}

std::uint64_t raidMapLayoutHash(
    const RaidGeneratedMapLayout &layout) noexcept
{
    if (layout.layoutVersion == 0U && layout.roadCells.empty() &&
        layout.fallbackReason == RaidMapFallbackReason::None)
    {
        return calculateLayoutHash(layout.ballisticBlockers);
    }
    constexpr std::uint64_t offset{1469598103934665603ULL};
    std::uint64_t hash = offset;
    hashValue(hash, layout.layoutVersion);
    hashValue(hash, layout.generationAttempt);
    hashValue(hash, layout.usedFallback ? 1U : 0U);
    hashValue(hash, static_cast<std::uint32_t>(layout.fallbackReason));
    hashValue(hash, static_cast<std::uint32_t>(layout.roadCells.size()));
    for (const RaidOutdoorRoadCell &cell : layout.roadCells)
    {
        hashValue(hash, cell.column);
        hashValue(hash, cell.row);
        hashValue(hash, static_cast<std::uint32_t>(cell.kind));
    }
    hashValue(hash, static_cast<std::uint32_t>(layout.ballisticBlockers.size()));
    for (const ContentRect &blocker : layout.ballisticBlockers)
    {
        hashValue(hash, static_cast<std::uint32_t>(
            std::lround(blocker.position.x * 10.0F)));
        hashValue(hash, static_cast<std::uint32_t>(
            std::lround(blocker.position.y * 10.0F)));
        hashValue(hash, static_cast<std::uint32_t>(
            std::lround(blocker.size.x * 10.0F)));
        hashValue(hash, static_cast<std::uint32_t>(
            std::lround(blocker.size.y * 10.0F)));
    }
    return hash;
}
