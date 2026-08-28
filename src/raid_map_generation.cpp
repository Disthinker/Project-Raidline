#include "raid_map_generation.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <numeric>
#include <optional>
#include <queue>
#include <set>
#include <stdexcept>
#include <string>
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

void hashString(std::uint64_t &hash, std::string_view value) noexcept
{
    hashValue(hash, static_cast<std::uint32_t>(value.size()));
    constexpr std::uint64_t prime{1099511628211ULL};
    for (const char character : value)
    {
        hash ^= static_cast<std::uint8_t>(character);
        hash *= prime;
    }
}

void hashFloat(std::uint64_t &hash, float value) noexcept
{
    hashValue(hash, static_cast<std::uint32_t>(
        std::lround(value * 10.0F)));
}

void hashRect(std::uint64_t &hash, ContentRect rect) noexcept
{
    hashFloat(hash, rect.position.x);
    hashFloat(hash, rect.position.y);
    hashFloat(hash, rect.size.x);
    hashFloat(hash, rect.size.y);
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
    if (layout.layoutVersion >= 3U)
    {
        return true;
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
    for (const ContentRect &blocker : layout.ballisticBlockers)
    {
        const float localLeft = blocker.position.x -
            map.walkableBounds.position.x;
        const float localTop = blocker.position.y -
            map.walkableBounds.position.y;
        const float localRight = localLeft + blocker.size.x;
        const float localBottom = localTop + blocker.size.y;
        const std::uint32_t firstColumn = std::min(
            definition.columns - 1U,
            static_cast<std::uint32_t>(std::max(0.0F,
                std::floor(localLeft / width))));
        const std::uint32_t lastColumn = std::min(
            definition.columns - 1U,
            static_cast<std::uint32_t>(std::max(0.0F,
                std::floor(std::max(localLeft, localRight - 0.001F) /
                           width))));
        const std::uint32_t firstRow = std::min(
            definition.rows - 1U,
            static_cast<std::uint32_t>(std::max(0.0F,
                std::floor(localTop / height))));
        const std::uint32_t lastRow = std::min(
            definition.rows - 1U,
            static_cast<std::uint32_t>(std::max(0.0F,
                std::floor(std::max(localTop, localBottom - 0.001F) /
                           height))));
        for (std::uint32_t row = firstRow; row <= lastRow; ++row)
        {
            for (std::uint32_t column = firstColumn;
                 column <= lastColumn;
                 ++column)
            {
                const Vec2 center{
                    map.walkableBounds.position.x +
                        (static_cast<float>(column) + 0.5F) * width,
                    map.walkableBounds.position.y +
                        (static_cast<float>(row) + 0.5F) * height};
                if (center.x > blocker.position.x &&
                    center.x < blocker.position.x + blocker.size.x &&
                    center.y > blocker.position.y &&
                    center.y < blocker.position.y + blocker.size.y)
                {
                    blocked[static_cast<std::size_t>(row) *
                            definition.columns + column] = true;
                }
            }
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

std::uint32_t randomBetween(
    Pcg32 &random,
    std::uint32_t minimum,
    std::uint32_t maximum)
{
    return minimum + random.bounded(maximum - minimum + 1U);
}

std::size_t cellIndex(
    const ProceduralOutdoorDefinition &definition,
    std::uint32_t column,
    std::uint32_t row) noexcept
{
    return static_cast<std::size_t>(row) * definition.columns + column;
}

void paintRoad(
    RoadSkeleton &roads,
    const ProceduralOutdoorDefinition &definition,
    Cell from,
    Cell to,
    RaidOutdoorRoadKind kind,
    std::uint32_t halfWidth)
{
    const auto paint = [&](std::uint32_t column, std::uint32_t row)
    {
        for (int offsetRow = -static_cast<int>(halfWidth);
             offsetRow <= static_cast<int>(halfWidth);
             ++offsetRow)
        {
            for (int offsetColumn = -static_cast<int>(halfWidth);
                 offsetColumn <= static_cast<int>(halfWidth);
                 ++offsetColumn)
            {
                const int candidateColumn =
                    static_cast<int>(column) + offsetColumn;
                const int candidateRow = static_cast<int>(row) + offsetRow;
                if (candidateColumn >= 0 && candidateRow >= 0)
                {
                    addRoadCell(
                        roads,
                        definition,
                        {static_cast<std::uint32_t>(candidateColumn),
                         static_cast<std::uint32_t>(candidateRow)},
                        kind);
                }
            }
        }
    };
    const std::uint32_t firstColumn = std::min(from.column, to.column);
    const std::uint32_t lastColumn = std::max(from.column, to.column);
    for (std::uint32_t column = firstColumn; column <= lastColumn; ++column)
    {
        paint(column, from.row);
    }
    const std::uint32_t firstRow = std::min(from.row, to.row);
    const std::uint32_t lastRow = std::max(from.row, to.row);
    for (std::uint32_t row = firstRow; row <= lastRow; ++row)
    {
        paint(to.column, row);
    }
}

void paintRoadWidth(
    RoadSkeleton &roads,
    const ProceduralOutdoorDefinition &definition,
    Cell from,
    Cell to,
    RaidOutdoorRoadKind kind,
    std::uint32_t width)
{
    const int firstOffset = -static_cast<int>(width / 2U);
    const int lastOffset = firstOffset + static_cast<int>(width) - 1;
    const std::uint32_t firstColumn = std::min(from.column, to.column);
    const std::uint32_t lastColumn = std::max(from.column, to.column);
    for (std::uint32_t column = firstColumn; column <= lastColumn; ++column)
    {
        for (int offset = firstOffset; offset <= lastOffset; ++offset)
        {
            const int row = static_cast<int>(from.row) + offset;
            if (row >= 0)
            {
                addRoadCell(
                    roads, definition,
                    {column, static_cast<std::uint32_t>(row)}, kind);
            }
        }
    }
    const std::uint32_t firstRow = std::min(from.row, to.row);
    const std::uint32_t lastRow = std::max(from.row, to.row);
    for (std::uint32_t row = firstRow; row <= lastRow; ++row)
    {
        for (int offset = firstOffset; offset <= lastOffset; ++offset)
        {
            const int column = static_cast<int>(to.column) + offset;
            if (column >= 0)
            {
                addRoadCell(
                    roads, definition,
                    {static_cast<std::uint32_t>(column), row}, kind);
            }
        }
    }
}

std::vector<RaidDistrictArchetypeDefinition> expandedDistricts(
    const ProceduralOutdoorDefinition &definition)
{
    std::vector<RaidDistrictArchetypeDefinition> result;
    for (const RaidDistrictArchetypeDefinition &archetype :
         definition.districtArchetypes)
    {
        for (std::uint32_t instance{};
             instance < archetype.instanceCount;
             ++instance)
        {
            RaidDistrictArchetypeDefinition expanded = archetype;
            expanded.id += "." + std::to_string(instance + 1U);
            if (archetype.instanceCount > 1U)
            {
                expanded.displayName +=
                    " " + std::to_string(instance + 1U);
            }
            expanded.instanceCount = 1U;
            result.push_back(std::move(expanded));
        }
    }
    return result;
}

struct DistrictField
{
    std::vector<std::uint16_t> owners;
    std::array<std::uint32_t, 5U> columnBoundaries{};
    std::array<std::uint32_t, 3U> rowBoundaries{};
};

DistrictField generateDistrictField(
    const ProceduralOutdoorDefinition &definition,
    const std::vector<RaidDistrictArchetypeDefinition> &districts,
    Pcg32 &random)
{
    DistrictField result;
    result.columnBoundaries = {
        0U,
        definition.districtColumns / 4U,
        definition.districtColumns / 2U,
        definition.districtColumns * 3U / 4U,
        definition.districtColumns};
    result.rowBoundaries = {
        0U,
        definition.districtRows / 2U,
        definition.districtRows};

    // Preserve a readable four-by-two zoning plan. Seed variation may nudge
    // internal boundaries by one coarse cell, but can no longer dissolve the
    // map into random Voronoi blobs.
    for (std::size_t boundary = 1U; boundary + 1U <
         result.columnBoundaries.size(); ++boundary)
    {
        const int jitter = static_cast<int>(random.bounded(3U)) - 1;
        const std::uint32_t minimum =
            result.columnBoundaries[boundary - 1U] + 1U;
        const std::uint32_t remainingSectors = static_cast<std::uint32_t>(
            result.columnBoundaries.size() - boundary - 1U);
        const std::uint32_t maximum =
            definition.districtColumns - remainingSectors;
        result.columnBoundaries[boundary] = std::clamp(
            static_cast<std::uint32_t>(
                std::max(0, static_cast<int>(
                    result.columnBoundaries[boundary]) + jitter)),
            minimum, maximum);
    }
    result.rowBoundaries[1U] = std::clamp(
        static_cast<std::uint32_t>(std::max(
            0, static_cast<int>(result.rowBoundaries[1U]) +
                   static_cast<int>(random.bounded(3U)) - 1)),
        1U, definition.districtRows - 1U);

    result.owners.resize(
        static_cast<std::size_t>(definition.districtColumns) *
        definition.districtRows);

    const auto indicesFor = [&](RaidDistrictKind kind)
    {
        std::vector<std::uint16_t> resultIndices;
        for (std::size_t index{}; index < districts.size(); ++index)
            if (districts[index].kind == kind)
                resultIndices.push_back(static_cast<std::uint16_t>(index));
        return resultIndices;
    };
    const auto industrial = indicesFor(RaidDistrictKind::Industrial);
    const auto logistics = indicesFor(RaidDistrictKind::Logistics);
    const auto highway = indicesFor(RaidDistrictKind::Highway);
    const auto openGround = indicesFor(RaidDistrictKind::OpenGround);
    const auto greenbelt = indicesFor(RaidDistrictKind::Greenbelt);
    const auto roadside = indicesFor(RaidDistrictKind::RoadsideService);
    const bool expectedArchetypes = districts.size() == 8U &&
        industrial.size() == 2U && logistics.size() == 2U &&
        highway.size() == 1U && openGround.size() == 1U &&
        greenbelt.size() == 1U && roadside.size() == 1U;

    std::array<std::uint16_t, 8U> sectors{};
    if (expectedArchetypes)
    {
        const bool reverseWorkingDistricts = random.bounded(2U) != 0U;
        sectors = reverseWorkingDistricts
            ? std::array<std::uint16_t, 8U>{
                  greenbelt.front(), logistics[0], industrial[0],
                  highway.front(), openGround.front(), industrial[1],
                  logistics[1], roadside.front()}
            : std::array<std::uint16_t, 8U>{
                  greenbelt.front(), industrial[0], logistics[0],
                  highway.front(), openGround.front(), logistics[1],
                  industrial[1], roadside.front()};
    }
    else
    {
        for (std::size_t index{}; index < sectors.size(); ++index)
            sectors[index] = static_cast<std::uint16_t>(
                std::min(index, districts.size() - 1U));
    }

    const bool mirrorHorizontally = random.bounded(2U) != 0U;
    const bool mirrorVertically = random.bounded(2U) != 0U;
    for (std::uint32_t sectorRow{}; sectorRow < 2U; ++sectorRow)
    {
        for (std::uint32_t sectorColumn{}; sectorColumn < 4U;
             ++sectorColumn)
        {
            const std::uint32_t sourceRow = mirrorVertically
                ? 1U - sectorRow : sectorRow;
            const std::uint32_t sourceColumn = mirrorHorizontally
                ? 3U - sectorColumn : sectorColumn;
            const std::uint16_t owner = sectors[
                static_cast<std::size_t>(sourceRow) * 4U + sourceColumn];
            for (std::uint32_t row = result.rowBoundaries[sectorRow];
                 row < result.rowBoundaries[sectorRow + 1U]; ++row)
                for (std::uint32_t column =
                         result.columnBoundaries[sectorColumn];
                     column < result.columnBoundaries[sectorColumn + 1U];
                     ++column)
                    result.owners[static_cast<std::size_t>(row) *
                        definition.districtColumns + column] = owner;
        }
    }
    return result;
}

std::uint16_t districtAtFineCell(
    const ProceduralOutdoorDefinition &definition,
    const DistrictField &field,
    std::uint32_t column,
    std::uint32_t row) noexcept
{
    const std::uint32_t districtColumn = std::min(
        definition.districtColumns - 1U,
        column * definition.districtColumns / definition.columns);
    const std::uint32_t districtRow = std::min(
        definition.districtRows - 1U,
        row * definition.districtRows / definition.rows);
    return field.owners[static_cast<std::size_t>(districtRow) *
        definition.districtColumns + districtColumn];
}

std::vector<RaidDistrictSnapshot> freezeDistricts(
    const MapDefinition &map,
    const DistrictField &field,
    const std::vector<RaidDistrictArchetypeDefinition> &definitions)
{
    const auto &procedural = map.proceduralOutdoor;
    std::vector<RaidDistrictSnapshot> result;
    result.reserve(definitions.size());
    const float districtCellWidth = map.walkableBounds.size.x /
        static_cast<float>(procedural.districtColumns);
    const float districtCellHeight = map.walkableBounds.size.y /
        static_cast<float>(procedural.districtRows);
    for (std::size_t index{}; index < definitions.size(); ++index)
    {
        RaidDistrictSnapshot district;
        district.instanceId = static_cast<std::uint16_t>(index + 1U);
        district.definitionId = definitions[index].id;
        district.displayName = definitions[index].displayName;
        district.kind = definitions[index].kind;
        std::uint64_t columnTotal{};
        std::uint64_t rowTotal{};
        std::uint64_t cellCount{};
        for (std::uint32_t row{}; row < procedural.districtRows; ++row)
        {
            std::uint32_t column{};
            while (column < procedural.districtColumns)
            {
                const std::size_t ownerIndex = static_cast<std::size_t>(row) *
                    procedural.districtColumns + column;
                if (field.owners[ownerIndex] != index)
                {
                    ++column;
                    continue;
                }
                const std::uint32_t first = column;
                while (column < procedural.districtColumns &&
                       field.owners[static_cast<std::size_t>(row) *
                           procedural.districtColumns + column] == index)
                {
                    columnTotal += column;
                    rowTotal += row;
                    ++cellCount;
                    ++column;
                }
                district.cells.push_back({
                    static_cast<std::uint16_t>(row),
                    static_cast<std::uint16_t>(first),
                    static_cast<std::uint16_t>(column - first)});
            }
        }
        if (cellCount > 0U)
        {
            district.labelPosition = {
                map.walkableBounds.position.x +
                    (static_cast<float>(columnTotal) /
                         static_cast<float>(cellCount) + 0.5F) *
                        districtCellWidth,
                map.walkableBounds.position.y +
                    (static_cast<float>(rowTotal) /
                         static_cast<float>(cellCount) + 0.5F) *
                        districtCellHeight};
        }
        result.push_back(std::move(district));
    }
    return result;
}

RoadSkeleton buildV3Roads(
    const ProceduralOutdoorDefinition &definition,
    const DistrictField &districtField,
    Pcg32 &random)
{
    RoadSkeleton roads;
    roads.ranks.resize(
        static_cast<std::size_t>(definition.columns) * definition.rows, 0U);
    const auto fineColumn = [&](std::uint32_t districtColumn)
    {
        return std::min(
            definition.columns - 1U,
            districtColumn * definition.columns /
                definition.districtColumns);
    };
    const auto fineRow = [&](std::uint32_t districtRow)
    {
        return std::min(
            definition.rows - 1U,
            districtRow * definition.rows / definition.districtRows);
    };
    const std::uint32_t spineColumn = fineColumn(
        districtField.columnBoundaries[2U]);
    const std::uint32_t spineRow = fineRow(
        districtField.rowBoundaries[1U]);
    paintRoadWidth(
        roads, definition,
        {0U, spineRow}, {definition.columns - 1U, spineRow},
        RaidOutdoorRoadKind::Primary, 4U);
    paintRoadWidth(
        roads, definition,
        {spineColumn, 0U}, {spineColumn, definition.rows - 1U},
        RaidOutdoorRoadKind::Primary, 4U);

    for (const std::size_t boundary : {1U, 3U})
    {
        const std::uint32_t column = fineColumn(
            districtField.columnBoundaries[boundary]);
        paintRoadWidth(
            roads, definition,
            {column, 0U}, {column, definition.rows - 1U},
            RaidOutdoorRoadKind::Secondary, 3U);
    }
    const std::uint32_t upperRouteRow = spineRow / 2U;
    const std::uint32_t lowerRouteRow =
        spineRow + (definition.rows - 1U - spineRow) / 2U;
    for (const std::uint32_t row : {upperRouteRow, lowerRouteRow})
        paintRoadWidth(
            roads, definition,
            {0U, row}, {definition.columns - 1U, row},
            RaidOutdoorRoadKind::Secondary, 3U);

    const std::uint32_t branchCount = randomBetween(
        random, definition.minimumBranchRoads,
        definition.maximumBranchRoads);
    const std::uint32_t baseBranchCount = 4U;
    for (std::uint32_t branch = baseBranchCount;
         branch < branchCount; ++branch)
    {
        if (branch % 2U == 0U)
        {
            const std::uint32_t sectorColumn = random.bounded(4U);
            const std::uint32_t sectorRow = random.bounded(2U);
            const std::uint32_t firstColumn = fineColumn(
                districtField.columnBoundaries[sectorColumn]);
            const std::uint32_t lastColumn = fineColumn(
                districtField.columnBoundaries[sectorColumn + 1U]);
            const std::uint32_t firstRow = fineRow(
                districtField.rowBoundaries[sectorRow]);
            const std::uint32_t lastRow = fineRow(
                districtField.rowBoundaries[sectorRow + 1U]);
            const std::uint32_t row = firstRow +
                (lastRow - firstRow) * (1U + random.bounded(3U)) / 4U;
            paintRoadWidth(
                roads, definition,
                {firstColumn, row}, {lastColumn, row},
                RaidOutdoorRoadKind::Secondary, 3U);
        }
        else
        {
            const std::uint32_t sectorColumn = random.bounded(4U);
            const std::uint32_t sectorRow = random.bounded(2U);
            const std::uint32_t firstColumn = fineColumn(
                districtField.columnBoundaries[sectorColumn]);
            const std::uint32_t lastColumn = fineColumn(
                districtField.columnBoundaries[sectorColumn + 1U]);
            const std::uint32_t firstRow = fineRow(
                districtField.rowBoundaries[sectorRow]);
            const std::uint32_t lastRow = fineRow(
                districtField.rowBoundaries[sectorRow + 1U]);
            const std::uint32_t column = firstColumn +
                (lastColumn - firstColumn) * (1U + random.bounded(3U)) / 4U;
            paintRoadWidth(
                roads, definition,
                {column, firstRow}, {column, lastRow},
                RaidOutdoorRoadKind::Secondary, 3U);
        }
    }
    return roads;
}

bool rectUsesRoadCells(
    const MapDefinition &map,
    const RoadSkeleton &roads,
    ContentRect rect) noexcept
{
    const auto &definition = map.proceduralOutdoor;
    const float cellWidth = map.walkableBounds.size.x /
        static_cast<float>(definition.columns);
    const float cellHeight = map.walkableBounds.size.y /
        static_cast<float>(definition.rows);
    const std::uint32_t firstColumn = std::min(
        definition.columns - 1U,
        static_cast<std::uint32_t>(std::max(0.0F,
            std::floor((rect.position.x - map.walkableBounds.position.x) /
                       cellWidth))));
    const std::uint32_t lastColumn = std::min(
        definition.columns - 1U,
        static_cast<std::uint32_t>(std::max(0.0F,
            std::floor((rect.position.x + rect.size.x - 0.001F -
                        map.walkableBounds.position.x) / cellWidth))));
    const std::uint32_t firstRow = std::min(
        definition.rows - 1U,
        static_cast<std::uint32_t>(std::max(0.0F,
            std::floor((rect.position.y - map.walkableBounds.position.y) /
                       cellHeight))));
    const std::uint32_t lastRow = std::min(
        definition.rows - 1U,
        static_cast<std::uint32_t>(std::max(0.0F,
            std::floor((rect.position.y + rect.size.y - 0.001F -
                        map.walkableBounds.position.y) / cellHeight))));
    for (std::uint32_t row = firstRow; row <= lastRow; ++row)
    {
        for (std::uint32_t column = firstColumn;
             column <= lastColumn;
             ++column)
        {
            if (roads.ranks[cellIndex(definition, column, row)] != 0U)
                return true;
        }
    }
    return false;
}

std::vector<RaidLandmarkPlacementSnapshot> placeLandmarks(
    const MapDefinition &map,
    const DistrictField &districtField,
    const std::vector<RaidDistrictArchetypeDefinition> &districtDefinitions,
    const RoadSkeleton &roads,
    Pcg32 &random)
{
    const auto &definition = map.proceduralOutdoor;
    const float cellWidth = map.walkableBounds.size.x /
        static_cast<float>(definition.columns);
    const float cellHeight = map.walkableBounds.size.y /
        static_cast<float>(definition.rows);
    std::vector<RaidLandmarkPlacementSnapshot> result;
    for (const RaidLandmarkTemplateDefinition &landmark :
         definition.landmarkTemplates)
    {
        const std::uint32_t widthCells = static_cast<std::uint32_t>(
            std::lround(landmark.footprintCells.x));
        const std::uint32_t heightCells = static_cast<std::uint32_t>(
            std::lround(landmark.footprintCells.y));
        std::optional<ContentRect> selected;
        std::uint16_t selectedDistrict{};
        for (std::uint32_t attempt{}; attempt < 1024U && !selected; ++attempt)
        {
            const std::uint32_t column = 2U + random.bounded(
                definition.columns - widthCells - 4U);
            const std::uint32_t row = 2U + random.bounded(
                definition.rows - heightCells - 4U);
            const std::uint16_t district = districtAtFineCell(
                definition, districtField,
                column + widthCells / 2U,
                row + heightCells / 2U);
            if (district >= districtDefinitions.size() ||
                districtDefinitions[district].kind != landmark.districtKind)
            {
                continue;
            }
            ContentRect bounds{
                {map.walkableBounds.position.x + column * cellWidth,
                 map.walkableBounds.position.y + row * cellHeight},
                {widthCells * cellWidth, heightCells * cellHeight}};
            if (rectUsesRoadCells(map, roads, bounds) ||
                std::any_of(result.begin(), result.end(),
                    [&](const RaidLandmarkPlacementSnapshot &existing)
                    { return overlaps(inflated(existing.bounds, cellWidth), bounds); }))
            {
                continue;
            }
            selected = bounds;
            selectedDistrict = static_cast<std::uint16_t>(district + 1U);
        }
        if (!selected)
            continue;
        RaidLandmarkPlacementSnapshot placement;
        placement.definitionId = landmark.id;
        placement.displayName = landmark.displayName;
        placement.bounds = *selected;
        placement.districtInstanceId = selectedDistrict;
        const ContentRect bounds = *selected;
        placement.structures = {
            {{bounds.position.x + bounds.size.x * 0.08F,
              bounds.position.y + bounds.size.y * 0.10F},
             {bounds.size.x * 0.38F, bounds.size.y * 0.34F}},
            {{bounds.position.x + bounds.size.x * 0.54F,
              bounds.position.y + bounds.size.y * 0.10F},
             {bounds.size.x * 0.36F, bounds.size.y * 0.34F}},
            {{bounds.position.x + bounds.size.x * 0.20F,
              bounds.position.y + bounds.size.y * 0.62F},
             {bounds.size.x * 0.60F, bounds.size.y * 0.22F}}};
        placement.roadSockets = {
            {bounds.position.x + bounds.size.x * 0.25F,
             bounds.position.y + bounds.size.y + cellHeight * 0.5F},
            {bounds.position.x + bounds.size.x * 0.75F,
             bounds.position.y + bounds.size.y + cellHeight * 0.5F}};
        result.push_back(std::move(placement));
    }
    return result;
}

Cell fineCellForPoint(
    const MapDefinition &map,
    Vec2 point) noexcept
{
    const auto &definition = map.proceduralOutdoor;
    const float cellWidth = map.walkableBounds.size.x /
        static_cast<float>(definition.columns);
    const float cellHeight = map.walkableBounds.size.y /
        static_cast<float>(definition.rows);
    return {
        std::min(definition.columns - 1U,
            static_cast<std::uint32_t>(std::max(0.0F,
                std::floor((point.x - map.walkableBounds.position.x) /
                           cellWidth)))),
        std::min(definition.rows - 1U,
            static_cast<std::uint32_t>(std::max(0.0F,
                std::floor((point.y - map.walkableBounds.position.y) /
                           cellHeight))))};
}

void connectPointToRoad(
    const MapDefinition &map,
    RoadSkeleton &roads,
    Vec2 point,
    RaidOutdoorRoadKind kind)
{
    const auto &definition = map.proceduralOutdoor;
    const Cell start = fineCellForPoint(map, point);
    Cell nearest = start;
    std::uint32_t nearestDistance =
        std::numeric_limits<std::uint32_t>::max();
    for (std::uint32_t row{}; row < definition.rows; ++row)
    {
        for (std::uint32_t column{}; column < definition.columns; ++column)
        {
            if (roads.ranks[cellIndex(definition, column, row)] == 0U)
                continue;
            const std::uint32_t distance =
                static_cast<std::uint32_t>(std::abs(
                    static_cast<int>(column) -
                    static_cast<int>(start.column))) +
                static_cast<std::uint32_t>(std::abs(
                    static_cast<int>(row) - static_cast<int>(start.row)));
            if (distance < nearestDistance)
            {
                nearestDistance = distance;
                nearest = {column, row};
            }
        }
    }
    paintRoadWidth(roads, definition, start, nearest, kind, 2U);
}

bool districtKindMatchesAnchor(
    RaidDistrictKind district,
    RaidMapAnchorKind anchor) noexcept
{
    switch (anchor)
    {
    case RaidMapAnchorKind::PlayerSpawn:
        return district == RaidDistrictKind::Greenbelt ||
            district == RaidDistrictKind::OpenGround;
    case RaidMapAnchorKind::NormalExtraction:
    case RaidMapAnchorKind::EmergencyExtraction:
        return district == RaidDistrictKind::Highway ||
            district == RaidDistrictKind::Logistics;
    case RaidMapAnchorKind::ConditionalExtraction:
    case RaidMapAnchorKind::Rescue:
        return district == RaidDistrictKind::RoadsideService ||
            district == RaidDistrictKind::Greenbelt;
    case RaidMapAnchorKind::HighRiskControl:
        return district == RaidDistrictKind::Industrial;
    case RaidMapAnchorKind::AdvancedResource:
        return district == RaidDistrictKind::Industrial ||
            district == RaidDistrictKind::Logistics;
    case RaidMapAnchorKind::InteriorEntrance:
        return district == RaidDistrictKind::Logistics;
    case RaidMapAnchorKind::Enemy:
    case RaidMapAnchorKind::PressureSpawn:
        return district == RaidDistrictKind::Industrial ||
            district == RaidDistrictKind::Logistics ||
            district == RaidDistrictKind::Highway;
    case RaidMapAnchorKind::SelfRecovery:
    case RaidMapAnchorKind::Loot:
        return true;
    }
    return true;
}

std::vector<RaidMapAnchorRequest> effectiveAnchorRequests(
    const RaidMapGenerationAnchors &anchors)
{
    if (!anchors.requests.empty())
        return anchors.requests;
    std::vector<RaidMapAnchorRequest> result{
        {std::string{kRaidAnchorPlayerSpawn}, RaidMapAnchorKind::PlayerSpawn,
         {50.0F, 50.0F}},
        {std::string{kRaidAnchorNormalExtraction},
         RaidMapAnchorKind::NormalExtraction,
         anchors.extractionPoint.size}};
    for (std::size_t index{}; index < anchors.reachablePoints.size(); ++index)
    {
        result.push_back({raidIndexedAnchorId("legacy", index),
                          RaidMapAnchorKind::Loot,
                          {32.0F, 32.0F}});
    }
    return result;
}

std::vector<RaidAnchorPlacementSnapshot> placeAnchors(
    const MapDefinition &map,
    const DistrictField &districtField,
    const std::vector<RaidDistrictArchetypeDefinition> &districtDefinitions,
    const RoadSkeleton &roads,
    const std::vector<RaidLandmarkPlacementSnapshot> &landmarks,
    const RaidMapGenerationAnchors &anchors,
    Pcg32 &random)
{
    const auto &definition = map.proceduralOutdoor;
    const float cellWidth = map.walkableBounds.size.x /
        static_cast<float>(definition.columns);
    const float cellHeight = map.walkableBounds.size.y /
        static_cast<float>(definition.rows);
    std::vector<Cell> roadCandidates;
    for (std::uint32_t row{2U}; row + 2U < definition.rows; ++row)
    {
        for (std::uint32_t column{2U};
             column + 2U < definition.columns;
             ++column)
        {
            if (roads.ranks[cellIndex(definition, column, row)] != 0U)
                roadCandidates.push_back({column, row});
        }
    }
    std::vector<RaidAnchorPlacementSnapshot> result;
    std::optional<Vec2> playerCenter;
    std::size_t interiorOrdinal{};
    for (const RaidMapAnchorRequest &request : effectiveAnchorRequests(anchors))
    {
        std::optional<Cell> selected;
        float selectedScore = request.kind == RaidMapAnchorKind::NormalExtraction
            ? -1.0F : std::numeric_limits<float>::infinity();
        if (request.kind == RaidMapAnchorKind::InteriorEntrance &&
            !landmarks.empty())
        {
            const RaidLandmarkPlacementSnapshot &landmark = landmarks.front();
            const Vec2 socket = landmark.roadSockets[
                interiorOrdinal % landmark.roadSockets.size()];
            selected = fineCellForPoint(map, socket);
            ++interiorOrdinal;
        }
        const std::size_t candidateOffset = roadCandidates.empty()
            ? 0U
            : random.bounded(static_cast<std::uint32_t>(
                  roadCandidates.size()));
        for (std::size_t probe{}; probe < roadCandidates.size(); ++probe)
        {
            const Cell candidate = roadCandidates[
                (probe + candidateOffset) % roadCandidates.size()];
            const std::uint16_t district = districtAtFineCell(
                definition, districtField, candidate.column, candidate.row);
            if (district >= districtDefinitions.size() ||
                !districtKindMatchesAnchor(
                    districtDefinitions[district].kind, request.kind))
            {
                continue;
            }
            const Vec2 center{
                map.walkableBounds.position.x +
                    (static_cast<float>(candidate.column) + 0.5F) * cellWidth,
                map.walkableBounds.position.y +
                    (static_cast<float>(candidate.row) + 0.5F) * cellHeight};
            const ContentRect bounds{
                {center.x - request.size.x * 0.5F,
                 center.y - request.size.y * 0.5F},
                request.size};
            if (std::any_of(result.begin(), result.end(),
                [&](const RaidAnchorPlacementSnapshot &existing)
                { return overlaps(inflated(existing.bounds, 100.0F), bounds); }))
            {
                continue;
            }
            if (request.kind == RaidMapAnchorKind::NormalExtraction &&
                playerCenter.has_value())
            {
                const float dx = center.x - playerCenter->x;
                const float dy = center.y - playerCenter->y;
                const float score = dx * dx + dy * dy;
                if (score > selectedScore)
                {
                    selectedScore = score;
                    selected = candidate;
                }
                continue;
            }
            selected = candidate;
            break;
        }
        if (!selected.has_value())
            return {};
        const Vec2 center{
            map.walkableBounds.position.x +
                (static_cast<float>(selected->column) + 0.5F) * cellWidth,
            map.walkableBounds.position.y +
                (static_cast<float>(selected->row) + 0.5F) * cellHeight};
        const std::uint16_t district = districtAtFineCell(
            definition, districtField, selected->column, selected->row);
        RaidAnchorPlacementSnapshot placement{
            request.id,
            request.kind,
            {{center.x - request.size.x * 0.5F,
              center.y - request.size.y * 0.5F}, request.size},
            static_cast<std::uint16_t>(district + 1U)};
        if (request.kind == RaidMapAnchorKind::PlayerSpawn)
            playerCenter = center;
        result.push_back(std::move(placement));
    }
    return result;
}

RaidTerrainKind baseTerrainForDistrict(RaidDistrictKind kind) noexcept
{
    switch (kind)
    {
    case RaidDistrictKind::Industrial:
    case RaidDistrictKind::Logistics:
    case RaidDistrictKind::RoadsideService:
        return RaidTerrainKind::Concrete;
    case RaidDistrictKind::Highway:
        return RaidTerrainKind::Asphalt;
    case RaidDistrictKind::Greenbelt:
        return RaidTerrainKind::Grass;
    case RaidDistrictKind::OpenGround:
        return RaidTerrainKind::Dirt;
    }
    return RaidTerrainKind::Dirt;
}

std::vector<RaidTerrainSpan> generateTerrain(
    const ProceduralOutdoorDefinition &definition,
    const DistrictField &districtField,
    const std::vector<RaidDistrictArchetypeDefinition> &districtDefinitions,
    const RoadSkeleton &roads,
    Pcg32 &random)
{
    std::vector<std::uint8_t> puddles(
        static_cast<std::size_t>(definition.columns) * definition.rows, 0U);
    const std::uint32_t puddleCount = randomBetween(
        random, definition.minimumPuddlePatches,
        definition.maximumPuddlePatches);
    for (std::uint32_t patch{}; patch < puddleCount; ++patch)
    {
        const std::uint32_t originColumn = random.bounded(definition.columns);
        const std::uint32_t originRow = random.bounded(definition.rows);
        const std::uint32_t width = 1U + random.bounded(3U);
        const std::uint32_t height = 1U + random.bounded(2U);
        for (std::uint32_t row = originRow;
             row < std::min(definition.rows, originRow + height);
             ++row)
        {
            for (std::uint32_t column = originColumn;
                 column < std::min(definition.columns, originColumn + width);
                 ++column)
            {
                if (roads.ranks[cellIndex(definition, column, row)] == 0U)
                    puddles[cellIndex(definition, column, row)] = 1U;
            }
        }
    }

    std::vector<RaidTerrainSpan> result;
    for (std::uint32_t row{}; row < definition.rows; ++row)
    {
        std::uint32_t column{};
        while (column < definition.columns)
        {
            const std::uint16_t district = districtAtFineCell(
                definition, districtField, column, row);
            RaidTerrainKind kind = baseTerrainForDistrict(
                districtDefinitions[district].kind);
            if (roads.ranks[cellIndex(definition, column, row)] != 0U)
                kind = RaidTerrainKind::Asphalt;
            else if (puddles[cellIndex(definition, column, row)] != 0U)
                kind = RaidTerrainKind::Puddle;
            const std::uint32_t first = column;
            const std::uint32_t chunkEnd = std::min(
                definition.columns,
                ((column / definition.chunkSizeCells) + 1U) *
                    definition.chunkSizeCells);
            while (column < chunkEnd)
            {
                const std::uint16_t nextDistrict = districtAtFineCell(
                    definition, districtField, column, row);
                RaidTerrainKind nextKind = baseTerrainForDistrict(
                    districtDefinitions[nextDistrict].kind);
                if (roads.ranks[cellIndex(definition, column, row)] != 0U)
                    nextKind = RaidTerrainKind::Asphalt;
                else if (puddles[cellIndex(definition, column, row)] != 0U)
                    nextKind = RaidTerrainKind::Puddle;
                if (nextKind != kind)
                    break;
                ++column;
            }
            result.push_back({
                static_cast<std::uint16_t>(row),
                static_cast<std::uint16_t>(first),
                static_cast<std::uint16_t>(column - first),
                kind});
        }
    }
    return result;
}

void appendProp(
    RaidGeneratedMapLayout &layout,
    RaidOutdoorPropKind kind,
    RaidOutdoorPropState state,
    ContentRect bounds,
    std::uint8_t quarterTurns,
    bool collidable)
{
    RaidOutdoorPropSnapshot prop{
        static_cast<std::uint32_t>(layout.props.size() + 1U),
        kind, state, bounds, quarterTurns, collidable};
    layout.props.push_back(prop);
    if (collidable)
        layout.ballisticBlockers.push_back(bounds);
}

void populateProps(
    const MapDefinition &map,
    const DistrictField &districtField,
    const std::vector<RaidDistrictArchetypeDefinition> &districtDefinitions,
    const RoadSkeleton &roads,
    Pcg32 &random,
    RaidGeneratedMapLayout &layout)
{
    const auto &definition = map.proceduralOutdoor;
    const float cellWidth = map.walkableBounds.size.x /
        static_cast<float>(definition.columns);
    const float cellHeight = map.walkableBounds.size.y /
        static_cast<float>(definition.rows);
    std::vector<std::uint8_t> occupied(roads.ranks.size(), 0U);
    const auto markRect = [&](ContentRect rect)
    {
        const Cell first = fineCellForPoint(map, rect.position);
        const Cell last = fineCellForPoint(
            map, {rect.position.x + rect.size.x - 0.001F,
                  rect.position.y + rect.size.y - 0.001F});
        for (std::uint32_t row = first.row; row <= last.row; ++row)
            for (std::uint32_t column = first.column;
                 column <= last.column; ++column)
                occupied[cellIndex(definition, column, row)] = 1U;
    };
    for (const RaidAnchorPlacementSnapshot &anchor : layout.anchorPlacements)
        markRect(inflated(anchor.bounds, 80.0F));
    for (const RaidLandmarkPlacementSnapshot &landmark : layout.landmarks)
    {
        markRect(landmark.bounds);
        for (const ContentRect &structure : landmark.structures)
            appendProp(layout, RaidOutdoorPropKind::Factory,
                       RaidOutdoorPropState::Weathered,
                       structure, 0U, true);
    }

    std::vector<Cell> roadCells;
    std::vector<Cell> openCells;
    for (std::uint32_t row{1U}; row + 1U < definition.rows; ++row)
    {
        for (std::uint32_t column{1U};
             column + 1U < definition.columns; ++column)
        {
            if (occupied[cellIndex(definition, column, row)] != 0U)
                continue;
            if (roads.ranks[cellIndex(definition, column, row)] != 0U)
                roadCells.push_back({column, row});
            else
                openCells.push_back({column, row});
        }
    }
    const std::uint32_t roadObstacleTarget = randomBetween(
        random, definition.minimumRoadObstacles,
        definition.maximumRoadObstacles);
    for (std::uint32_t index{};
         index < roadObstacleTarget && !roadCells.empty(); ++index)
    {
        const std::size_t selected = random.bounded(
            static_cast<std::uint32_t>(roadCells.size()));
        const Cell cell = roadCells[selected];
        roadCells[selected] = roadCells.back();
        roadCells.pop_back();
        if (occupied[cellIndex(definition, cell.column, cell.row)] != 0U)
            continue;
        occupied[cellIndex(definition, cell.column, cell.row)] = 1U;
        const bool barrier = index % 7U == 0U;
        const bool truck = !barrier && index % 4U == 0U;
        const Vec2 size = barrier
            ? Vec2{64.0F, 18.0F}
            : (truck ? Vec2{68.0F, 42.0F} : Vec2{48.0F, 30.0F});
        const Vec2 center{
            map.walkableBounds.position.x +
                (static_cast<float>(cell.column) + 0.5F) * cellWidth,
            map.walkableBounds.position.y +
                (static_cast<float>(cell.row) + 0.5F) * cellHeight};
        appendProp(
            layout,
            barrier ? RaidOutdoorPropKind::RoadBarrier
                    : (truck ? RaidOutdoorPropKind::Truck
                             : RaidOutdoorPropKind::Car),
            index % 3U == 0U ? RaidOutdoorPropState::Damaged
                             : RaidOutdoorPropState::Abandoned,
            {{center.x - size.x * 0.5F, center.y - size.y * 0.5F}, size},
            static_cast<std::uint8_t>(random.bounded(4U)), true);
    }

    const std::uint32_t blockerTarget = randomBetween(
        random, definition.minimumBlockers, definition.maximumBlockers);
    const std::uint32_t largeBuildingTarget = randomBetween(random, 18U, 28U);
    std::uint32_t largeBuildingCount = static_cast<std::uint32_t>(
        std::count_if(
            layout.props.begin(), layout.props.end(),
            [](const RaidOutdoorPropSnapshot &prop)
            {
                return prop.kind == RaidOutdoorPropKind::Factory ||
                    prop.kind == RaidOutdoorPropKind::Warehouse;
            }));

    // Place the large silhouettes on a district-aligned compound grid before
    // filling small blockers. They remain seed-varied, but read as planned
    // factory/warehouse blocks instead of isolated random rectangles.
    std::vector<Cell> compoundCandidates;
    const std::uint32_t compoundColumnPhase = 2U + random.bounded(4U);
    const std::uint32_t compoundRowPhase = 2U + random.bounded(3U);
    for (std::uint32_t row = compoundRowPhase;
         row + 4U < definition.rows; row += 7U)
        for (std::uint32_t column = compoundColumnPhase;
             column + 6U < definition.columns; column += 9U)
        {
            const std::uint16_t district = districtAtFineCell(
                definition, districtField, column, row);
            const RaidDistrictKind kind = districtDefinitions[district].kind;
            if (kind == RaidDistrictKind::Industrial ||
                kind == RaidDistrictKind::Logistics)
                compoundCandidates.push_back({column, row});
        }
    for (std::size_t remaining = compoundCandidates.size();
         remaining > 1U; --remaining)
    {
        const std::size_t selected = random.bounded(
            static_cast<std::uint32_t>(remaining));
        std::swap(compoundCandidates[selected],
                  compoundCandidates[remaining - 1U]);
    }
    for (const Cell origin : compoundCandidates)
    {
        if (largeBuildingCount >= largeBuildingTarget)
            break;
        const std::uint16_t district = districtAtFineCell(
            definition, districtField, origin.column, origin.row);
        const RaidDistrictKind districtKind =
            districtDefinitions[district].kind;
        const std::uint32_t widthCells = 4U + random.bounded(3U);
        const std::uint32_t heightCells = 3U + random.bounded(2U);
        bool legal = origin.column + widthCells < definition.columns &&
            origin.row + heightCells < definition.rows;
        for (std::uint32_t row = origin.row;
             legal && row < origin.row + heightCells; ++row)
            for (std::uint32_t column = origin.column;
                 legal && column < origin.column + widthCells; ++column)
                legal =
                    districtAtFineCell(
                        definition, districtField, column, row) == district &&
                    occupied[cellIndex(definition, column, row)] == 0U &&
                    roads.ranks[cellIndex(definition, column, row)] == 0U;
        if (!legal)
            continue;
        for (std::uint32_t row = origin.row;
             row < origin.row + heightCells; ++row)
            for (std::uint32_t column = origin.column;
                 column < origin.column + widthCells; ++column)
                occupied[cellIndex(definition, column, row)] = 1U;
        const ContentRect bounds{
            {map.walkableBounds.position.x + origin.column * cellWidth + 7.0F,
             map.walkableBounds.position.y + origin.row * cellHeight + 7.0F},
            {widthCells * cellWidth - 14.0F,
             heightCells * cellHeight - 14.0F}};
        appendProp(
            layout,
            districtKind == RaidDistrictKind::Industrial
                ? RaidOutdoorPropKind::Factory
                : RaidOutdoorPropKind::Warehouse,
            RaidOutdoorPropState::Weathered,
            bounds,
            bounds.size.x >= bounds.size.y ? 0U : 1U,
            true);
        ++largeBuildingCount;
    }

    for (std::uint32_t attempt{};
         layout.ballisticBlockers.size() < blockerTarget &&
         attempt < blockerTarget * 80U && !openCells.empty();
         ++attempt)
    {
        const Cell origin = openCells[random.bounded(
            static_cast<std::uint32_t>(openCells.size()))];
        if (occupied[cellIndex(definition, origin.column, origin.row)] != 0U)
            continue;
        const std::uint16_t district = districtAtFineCell(
            definition, districtField, origin.column, origin.row);
        const RaidDistrictKind districtKind =
            districtDefinitions[district].kind;
        if ((districtKind == RaidDistrictKind::Greenbelt ||
             districtKind == RaidDistrictKind::OpenGround) &&
            random.bounded(4U) != 0U)
            continue;
        constexpr std::uint32_t widthCells{1U};
        constexpr std::uint32_t heightCells{1U};
        if (origin.column + widthCells >= definition.columns ||
            origin.row + heightCells >= definition.rows)
            continue;
        bool legal = true;
        for (std::uint32_t row = origin.row;
             row < origin.row + heightCells; ++row)
            for (std::uint32_t column = origin.column;
                 column < origin.column + widthCells; ++column)
                legal = legal &&
                    occupied[cellIndex(definition, column, row)] == 0U &&
                    roads.ranks[cellIndex(definition, column, row)] == 0U;
        if (!legal)
            continue;
        for (std::uint32_t row = origin.row;
             row < origin.row + heightCells; ++row)
            for (std::uint32_t column = origin.column;
                 column < origin.column + widthCells; ++column)
                occupied[cellIndex(definition, column, row)] = 1U;
        const ContentRect bounds{
            {map.walkableBounds.position.x + origin.column * cellWidth + 7.0F,
             map.walkableBounds.position.y + origin.row * cellHeight + 7.0F},
            {widthCells * cellWidth - 14.0F,
             heightCells * cellHeight - 14.0F}};
        RaidOutdoorPropKind kind = RaidOutdoorPropKind::Container;
        if (districtKind == RaidDistrictKind::Industrial)
            kind = layout.ballisticBlockers.size() % 3U == 0U
                ? RaidOutdoorPropKind::EngineeringEquipment
                : RaidOutdoorPropKind::Container;
        else if (districtKind == RaidDistrictKind::Highway ||
                 districtKind == RaidDistrictKind::OpenGround ||
                 districtKind == RaidDistrictKind::Greenbelt)
            kind = RaidOutdoorPropKind::Container;
        else if (districtKind == RaidDistrictKind::RoadsideService)
            kind = layout.ballisticBlockers.size() % 2U == 0U
                ? RaidOutdoorPropKind::EngineeringEquipment
                : RaidOutdoorPropKind::Container;
        appendProp(layout, kind, RaidOutdoorPropState::Weathered,
                   bounds, static_cast<std::uint8_t>(random.bounded(4U)), true);
    }

    const std::uint32_t decorativeTarget = randomBetween(
        random, definition.minimumDecorativeProps,
        definition.maximumDecorativeProps);
    for (std::uint32_t index{};
         index < decorativeTarget && !openCells.empty(); ++index)
    {
        const Cell cell = openCells[random.bounded(
            static_cast<std::uint32_t>(openCells.size()))];
        const Vec2 center{
            map.walkableBounds.position.x +
                (static_cast<float>(cell.column) + 0.2F +
                 static_cast<float>(random.bounded(60U)) / 100.0F) *
                    cellWidth,
            map.walkableBounds.position.y +
                (static_cast<float>(cell.row) + 0.2F +
                 static_cast<float>(random.bounded(60U)) / 100.0F) *
                    cellHeight};
        const float size = 10.0F + static_cast<float>(random.bounded(20U));
        appendProp(
            layout,
            index % 5U == 0U ? RaidOutdoorPropKind::EngineeringEquipment
                             : RaidOutdoorPropKind::Debris,
            RaidOutdoorPropState::Weathered,
            {{center.x - size * 0.5F, center.y - size * 0.5F},
             {size, size}},
            static_cast<std::uint8_t>(random.bounded(4U)), false);
    }
}

void finalizeRoadCells(
    const ProceduralOutdoorDefinition &definition,
    RoadSkeleton &roads)
{
    roads.cells.clear();
    for (std::uint32_t row{}; row < definition.rows; ++row)
    {
        for (std::uint32_t column{}; column < definition.columns; ++column)
        {
            const std::uint8_t rank = roads.ranks[
                cellIndex(definition, column, row)];
            if (rank > 0U)
            {
                roads.cells.push_back({
                    static_cast<std::uint16_t>(column),
                    static_cast<std::uint16_t>(row),
                    static_cast<RaidOutdoorRoadKind>(rank - 1U)});
            }
        }
    }
}

RaidGeneratedMapLayout generateV3Layout(
    const MapDefinition &map,
    std::uint64_t raidSeed,
    const RaidMapGenerationAnchors &anchors,
    std::uint32_t attempt)
{
    const auto &definition = map.proceduralOutdoor;
    Pcg32 districtRandom{
        raidSeed ^ (static_cast<std::uint64_t>(attempt) << 32U),
        0x6469737472696374ULL};
    Pcg32 roadRandom{raidSeed, 0x726f6164732d7633ULL + attempt};
    Pcg32 landmarkRandom{raidSeed, 0x6c616e646d61726bULL + attempt};
    Pcg32 anchorRandom{raidSeed, 0x616e63686f72732dULL + attempt};
    Pcg32 terrainRandom{raidSeed, 0x7465727261696e73ULL + attempt};
    Pcg32 propRandom{raidSeed, 0x70726f70732d7633ULL + attempt};
    const std::vector<RaidDistrictArchetypeDefinition> districts =
        expandedDistricts(definition);
    const DistrictField districtField = generateDistrictField(
        definition, districts, districtRandom);
    RoadSkeleton roads = buildV3Roads(
        definition, districtField, roadRandom);
    std::vector<RaidLandmarkPlacementSnapshot> landmarks = placeLandmarks(
        map, districtField, districts, roads, landmarkRandom);
    if (landmarks.size() != definition.landmarkTemplates.size())
        return {};
    for (const RaidLandmarkPlacementSnapshot &landmark : landmarks)
        for (Vec2 socket : landmark.roadSockets)
            connectPointToRoad(map, roads, socket, RaidOutdoorRoadKind::Access);
    const std::vector<RaidAnchorPlacementSnapshot> placements = placeAnchors(
        map, districtField, districts, roads, landmarks, anchors, anchorRandom);
    if (placements.size() != effectiveAnchorRequests(anchors).size())
        return {};
    for (const RaidAnchorPlacementSnapshot &placement : placements)
    {
        connectPointToRoad(
            map, roads,
            {placement.bounds.position.x + placement.bounds.size.x * 0.5F,
             placement.bounds.position.y + placement.bounds.size.y * 0.5F},
            RaidOutdoorRoadKind::Access);
    }
    finalizeRoadCells(definition, roads);

    RaidGeneratedMapLayout layout;
    layout.layoutVersion = definition.layoutVersion;
    layout.generationAttempt = attempt;
    layout.districts = freezeDistricts(map, districtField, districts);
    layout.roadCells = roads.cells;
    layout.anchorPlacements = placements;
    layout.landmarks = std::move(landmarks);
    layout.terrainSpans = generateTerrain(
        definition, districtField, districts, roads, terrainRandom);
    populateProps(
        map, districtField, districts, roads, propRandom, layout);
    layout.layoutHash = raidMapLayoutHash(layout);
    return layout;
}
}

std::string raidIndexedAnchorId(
    std::string_view prefix,
    std::size_t index)
{
    return std::string{prefix} + ":" + std::to_string(index);
}

const RaidAnchorPlacementSnapshot *findRaidAnchorPlacement(
    const RaidGeneratedMapLayout &layout,
    std::string_view id) noexcept
{
    const auto found = std::find_if(
        layout.anchorPlacements.begin(), layout.anchorPlacements.end(),
        [&](const RaidAnchorPlacementSnapshot &placement)
        { return placement.id == id; });
    return found == layout.anchorPlacements.end() ? nullptr : &*found;
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
    if (definition.layoutVersion >= 3U)
    {
        for (std::uint32_t attempt = 1U;
             attempt <= definition.maximumAttempts;
             ++attempt)
        {
            RaidGeneratedMapLayout layout = generateV3Layout(
                map, raidSeed, anchors, attempt);
            if (layout.layoutVersion != definition.layoutVersion ||
                layout.districts.size() != 8U ||
                layout.landmarks.size() != 3U ||
                layout.anchorPlacements.size() !=
                    effectiveAnchorRequests(anchors).size() ||
                layout.ballisticBlockers.size() <
                    definition.minimumBlockers ||
                layout.ballisticBlockers.size() >
                    definition.maximumBlockers ||
                !roadLayoutIsValid(map, layout, anchors))
            {
                continue;
            }
            RaidMapGenerationAnchors frozenAnchors;
            if (const auto *player = findRaidAnchorPlacement(
                    layout, kRaidAnchorPlayerSpawn))
            {
                frozenAnchors.playerSpawn = {
                    player->bounds.position.x,
                    player->bounds.position.y};
            }
            if (const auto *extraction = findRaidAnchorPlacement(
                    layout, kRaidAnchorNormalExtraction))
            {
                frozenAnchors.extractionPoint = extraction->bounds;
            }
            for (const RaidAnchorPlacementSnapshot &placement :
                 layout.anchorPlacements)
            {
                frozenAnchors.reachablePoints.push_back({
                    placement.bounds.position.x +
                        placement.bounds.size.x * 0.5F,
                    placement.bounds.position.y +
                        placement.bounds.size.y * 0.5F});
            }
            if (layoutConnects(map, layout, frozenAnchors))
            {
                layout.layoutHash = raidMapLayoutHash(layout);
                return layout;
            }
        }

        MapDefinition fallbackMap = map;
        fallbackMap.proceduralOutdoor.minimumBlockers = 700U;
        fallbackMap.proceduralOutdoor.maximumBlockers = 700U;
        fallbackMap.proceduralOutdoor.minimumDecorativeProps = 1200U;
        fallbackMap.proceduralOutdoor.maximumDecorativeProps = 1200U;
        fallbackMap.proceduralOutdoor.minimumRoadObstacles = 140U;
        fallbackMap.proceduralOutdoor.maximumRoadObstacles = 140U;
        fallbackMap.proceduralOutdoor.minimumPuddlePatches = 60U;
        fallbackMap.proceduralOutdoor.maximumPuddlePatches = 60U;
        RaidGeneratedMapLayout fallback = generateV3Layout(
            fallbackMap,
            0x46524f4e54494552ULL,
            anchors,
            1U);
        if (fallback.layoutVersion == 0U)
        {
            throw std::runtime_error{
                "Raid v3 fallback layout could not be constructed"};
        }
        fallback.generationAttempt = definition.maximumAttempts;
        fallback.usedFallback = true;
        fallback.fallbackReason = RaidMapFallbackReason::AttemptsExhausted;
        fallback.layoutHash = raidMapLayoutHash(fallback);
        return fallback;
    }
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
    if (map.proceduralOutdoor.enabled && layout.layoutVersion >= 3U)
    {
        RaidMapGenerationAnchors frozenAnchors;
        const auto *player = findRaidAnchorPlacement(
            layout, kRaidAnchorPlayerSpawn);
        const auto *extraction = findRaidAnchorPlacement(
            layout, kRaidAnchorNormalExtraction);
        if (player == nullptr || extraction == nullptr)
            return false;
        frozenAnchors.playerSpawn = player->bounds.position;
        frozenAnchors.extractionPoint = extraction->bounds;
        for (const RaidAnchorPlacementSnapshot &placement :
             layout.anchorPlacements)
        {
            frozenAnchors.reachablePoints.push_back({
                placement.bounds.position.x + placement.bounds.size.x * 0.5F,
                placement.bounds.position.y + placement.bounds.size.y * 0.5F});
        }
        return roadLayoutIsValid(map, layout, frozenAnchors) &&
            layoutConnects(map, layout, frozenAnchors);
    }
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
    hashValue(hash, static_cast<std::uint32_t>(layout.districts.size()));
    for (const RaidDistrictSnapshot &district : layout.districts)
    {
        hashValue(hash, district.instanceId);
        hashString(hash, district.definitionId);
        hashString(hash, district.displayName);
        hashValue(hash, static_cast<std::uint32_t>(district.kind));
        hashFloat(hash, district.labelPosition.x);
        hashFloat(hash, district.labelPosition.y);
        for (const RaidGridSpan &span : district.cells)
        {
            hashValue(hash, span.row);
            hashValue(hash, span.firstColumn);
            hashValue(hash, span.length);
        }
    }
    hashValue(hash, static_cast<std::uint32_t>(layout.terrainSpans.size()));
    for (const RaidTerrainSpan &span : layout.terrainSpans)
    {
        hashValue(hash, span.row);
        hashValue(hash, span.firstColumn);
        hashValue(hash, span.length);
        hashValue(hash, static_cast<std::uint32_t>(span.kind));
    }
    hashValue(hash, static_cast<std::uint32_t>(layout.roadCells.size()));
    for (const RaidOutdoorRoadCell &cell : layout.roadCells)
    {
        hashValue(hash, cell.column);
        hashValue(hash, cell.row);
        hashValue(hash, static_cast<std::uint32_t>(cell.kind));
    }
    hashValue(hash, static_cast<std::uint32_t>(layout.props.size()));
    for (const RaidOutdoorPropSnapshot &prop : layout.props)
    {
        hashValue(hash, prop.instanceId);
        hashValue(hash, static_cast<std::uint32_t>(prop.kind));
        hashValue(hash, static_cast<std::uint32_t>(prop.state));
        hashRect(hash, prop.bounds);
        hashValue(hash, prop.quarterTurns);
        hashValue(hash, prop.collidable ? 1U : 0U);
    }
    hashValue(hash, static_cast<std::uint32_t>(
        layout.anchorPlacements.size()));
    for (const RaidAnchorPlacementSnapshot &placement :
         layout.anchorPlacements)
    {
        hashString(hash, placement.id);
        hashValue(hash, static_cast<std::uint32_t>(placement.kind));
        hashRect(hash, placement.bounds);
        hashValue(hash, placement.districtInstanceId);
    }
    hashValue(hash, static_cast<std::uint32_t>(layout.landmarks.size()));
    for (const RaidLandmarkPlacementSnapshot &landmark : layout.landmarks)
    {
        hashString(hash, landmark.definitionId);
        hashString(hash, landmark.displayName);
        hashRect(hash, landmark.bounds);
        hashValue(hash, landmark.districtInstanceId);
        for (ContentRect structure : landmark.structures)
            hashRect(hash, structure);
        for (Vec2 socket : landmark.roadSockets)
        {
            hashFloat(hash, socket.x);
            hashFloat(hash, socket.y);
        }
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
