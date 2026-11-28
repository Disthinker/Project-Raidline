#include "raid_map_generation.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <numeric>
#include <map>
#include <optional>
#include <queue>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>

#include "stable_random.h"
#include "raid_district_fill.h"
#include "raid_hospital_outdoor.h"

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
    bool traceNetwork{};
    std::vector<RaidRoadSegment> segments;
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

std::vector<bool> reachableLayoutCells(
    const MapDefinition &map,
    const RaidGeneratedMapLayout &layout,
    Vec2 playerSpawn)
{
    const ProceduralOutdoorDefinition &definition = map.proceduralOutdoor;
    if (definition.columns == 0U || definition.rows == 0U)
    {
        return {};
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

    const auto start = cellForPoint(map, definition, playerSpawn);
    if (!start.has_value())
    {
        return {};
    }
    const std::size_t startIndex = static_cast<std::size_t>(start->row) *
        definition.columns + start->column;
    if (blocked[startIndex])
    {
        return {};
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

    return visited;
}

bool layoutConnects(
    const MapDefinition &map,
    const RaidGeneratedMapLayout &layout,
    const RaidMapGenerationAnchors &anchors) noexcept
{
    const ProceduralOutdoorDefinition &definition = map.proceduralOutdoor;
    const auto visited = reachableLayoutCells(map, layout, anchors.playerSpawn);
    if (visited.empty())
        return false;
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

int artCoordinate(std::uint32_t cell)
{
    return static_cast<int>((cell * 80U + 40U + 32U) / 64U);
}

void traceRoad(RoadSkeleton &roads, const ProceduralOutdoorDefinition &definition,
               RaidRoadSegment edge)
{
    if (edge.x1 == edge.x2 && edge.y1 == edge.y2)
        return;
    if (edge.x1 > edge.x2 || edge.y1 > edge.y2)
    {
        std::swap(edge.x1, edge.x2);
        std::swap(edge.y1, edge.y2);
    }
    roads.segments.push_back(edge);
    // Sample navigation cell centers in the actual paved footprint. Tile
    // bounds are half-open, including the square apron around each end.
    const int half = raidRoadHalfWidthTiles(edge.kind);
    const int left = (edge.x1 - half) * 64, right = (edge.x2 + half) * 64;
    const int top = (edge.y1 - half) * 64, bottom = (edge.y2 + half) * 64;
    for (int y = std::max(0, top / 80);
         y < static_cast<int>(definition.rows) && y * 80 + 40 < bottom; ++y)
        for (int x = std::max(0, left / 80);
             x < static_cast<int>(definition.columns) && x * 80 + 40 < right; ++x)
            if (x * 80 + 40 >= left && y * 80 + 40 >= top)
                addRoadCell(roads, definition, {static_cast<unsigned>(x), static_cast<unsigned>(y)},
                            edge.kind);
}

std::vector<RaidRoadSegment> freezeRoadNetwork(const std::vector<RaidRoadSegment> &raw)
{
    using Point = std::pair<int, int>;
    // East and south unit edges have exactly one owner, including collinear
    // overlap. Split straight runs only at a junction or a change of width.
    std::map<Point, std::array<int, 4>> arms;
    for (const auto &s : raw)
    {
        const bool horizontal = s.y1 == s.y2;
        const int d = horizontal ? 1 : 2, rank = 1 + static_cast<int>(s.kind);
        for (int x = s.x1, y = s.y1; x < s.x2 || y < s.y2; x += horizontal, y += !horizontal)
        {
            auto &a = arms[{x, y}][d], &b = arms[{x + horizontal, y + !horizontal}][(d + 2) % 4];
            a = b = std::max(a, rank);
        }
    }
    std::vector<RaidRoadSegment> result;
    for (const auto &[pos, links] : arms)
        for (int d : {1, 2})
        {
            const int rank = links[d];
            if (!rank || (links[(d + 2) % 4] == rank && !links[(d + 1) % 4] && !links[(d + 3) % 4]))
                continue;
            auto [x, y] = pos;
            do
            {
                x += d == 1;
                y += d == 2;
                const auto &next = arms.at({x, y});
                if (next[d] != rank || next[(d + 1) % 4] || next[(d + 3) % 4])
                    break;
            } while (true);
            result.push_back(
                {pos.first, pos.second, x, y, static_cast<RaidOutdoorRoadKind>(rank - 1)});
        }
    return result;
}

void paintRoadWidth(
    RoadSkeleton &roads,
    const ProceduralOutdoorDefinition &definition,
    Cell from,
    Cell to,
    RaidOutdoorRoadKind kind,
    std::uint32_t width)
{
    if (roads.traceNetwork)
    {
        const int x1 = artCoordinate(from.column), y1 = artCoordinate(from.row);
        const int x2 = artCoordinate(to.column), y2 = artCoordinate(to.row);
        traceRoad(roads, definition, {x1, y1, x2, y1, kind});
        traceRoad(roads, definition, {x2, y1, x2, y2, kind});
        return;
    }
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

RoadSkeleton buildV3Roads(const ProceduralOutdoorDefinition &definition,
                          const DistrictField &districtField, Pcg32 &random, bool traceNetwork)
{
    RoadSkeleton roads;
    roads.traceNetwork = traceNetwork;
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
    if (roads.traceNetwork)
    {
        // A service lane returns to the same secondary street through two
        // right-angle turns. Search clear frontage before laying any segment.
        const int street = artCoordinate(upperRouteRow);
        const int width = static_cast<int>(definition.columns * 80 / 64);
        const int length = 16 + static_cast<int>(random.bounded(9));
        const int depth = 8 + static_cast<int>(random.bounded(5));
        const int start = static_cast<int>(random.bounded(width));
        for (int i = 0; i < width; ++i)
        {
            const int x = (start + i) % width;
            if (x < 6 || x + length + 6 >= width)
                continue;
            const bool clear = std::none_of(
                roads.segments.begin(), roads.segments.end(), [&](const RaidRoadSegment &s) {
                    if (s.y1 == street && s.y2 == street)
                        return false;
                    const int margin = raidRoadHalfWidthTiles(s.kind) + 2;
                    return s.x2 + margin >= x && s.x1 - margin <= x + length &&
                           s.y2 + margin >= street && s.y1 - margin <= street + depth;
                });
            if (!clear)
                continue;
            traceRoad(roads, definition,
                      {x, street, x, street + depth, RaidOutdoorRoadKind::Access});
            traceRoad(roads, definition,
                      {x, street + depth, x + length, street + depth, RaidOutdoorRoadKind::Access});
            traceRoad(
                roads, definition,
                {x + length, street, x + length, street + depth, RaidOutdoorRoadKind::Access});
            break;
        }
    }
    return roads;
}

bool rectUsesRoadCells(
    const MapDefinition &map,
    const RoadSkeleton &roads,
    ContentRect rect) noexcept
{
    if (roads.traceNetwork)
    {
        for (const auto &edge : roads.segments)
        {
            const int half = raidRoadHalfWidthTiles(edge.kind);
            const ContentRect paved{
                {map.walkableBounds.position.x + (edge.x1 - half) * 64.0F,
                 map.walkableBounds.position.y + (edge.y1 - half) * 64.0F},
                {(edge.x2 - edge.x1 + 2 * half) * 64.0F, (edge.y2 - edge.y1 + 2 * half) * 64.0F}};
            if (overlaps(rect, paved))
                return true;
        }
    }
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
        if (!landmark.structures.empty())
        {
            for (const ContentRect &structure : landmark.structures)
                placement.structures.push_back({
                    {bounds.position.x + bounds.size.x * structure.position.x,
                     bounds.position.y + bounds.size.y * structure.position.y},
                    {bounds.size.x * structure.size.x,
                     bounds.size.y * structure.size.y}});
        }
        else placement.structures = {
            {{bounds.position.x + bounds.size.x * 0.06F,
              bounds.position.y + bounds.size.y * 0.08F},
             {bounds.size.x * 0.58F, bounds.size.y * 0.40F}},
            {{bounds.position.x + bounds.size.x * 0.68F,
              bounds.position.y + bounds.size.y * 0.08F},
             {bounds.size.x * 0.26F, bounds.size.y * 0.40F}},
            {{bounds.position.x + bounds.size.x * 0.16F,
              bounds.position.y + bounds.size.y * 0.60F},
             {bounds.size.x * 0.68F, bounds.size.y * 0.30F}}};
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
    if (roads.traceNetwork)
    {
        if (roads.ranks[cellIndex(definition, start.column, start.row)])
            return;
        const int x = artCoordinate(start.column), y = artCoordinate(start.row);
        int nx = x, ny = y, distance = std::numeric_limits<int>::max();
        for (const auto &s : roads.segments)
        {
            const int cx = std::clamp(x, s.x1, s.x2), cy = std::clamp(y, s.y1, s.y2);
            const int cost = std::abs(x - cx) + std::abs(y - cy);
            if (cost < distance)
            {
                distance = cost;
                nx = cx;
                ny = cy;
            }
        }
        traceRoad(roads, definition, {x, y, nx, y, kind});
        traceRoad(roads, definition, {nx, y, nx, ny, kind});
        return;
    }
    if (usesHospitalOutdoorPlan(map) && roads.ranks[cellIndex(definition, start.column, start.row)] != 0U)
        return;
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
    case RaidMapAnchorKind::CredentialExtraction:
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
    case RaidMapAnchorKind::ResourcePoint:
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
    Pcg32 &random,
    std::optional<Vec2> playerApproach)
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
    std::vector<Vec2> protectedDoorSockets;
    if (usesHospitalOutdoorPlan(map) && !landmarks.empty())
    {
        for (const auto &landmark : landmarks)
        {
            if (&landmark == &landmarks.front() ||
                (definition.layoutVersion == kHospitalLandmarkInteriorLayoutVersion &&
                 std::any_of(map.interiors.begin(), map.interiors.end(), [&](const auto &space)
                 { return space.landmarkDefinitionId == landmark.definitionId; })))
                protectedDoorSockets.insert(protectedDoorSockets.end(), landmark.roadSockets.begin(), landmark.roadSockets.end());
        }
    }
    std::vector<RaidAnchorPlacementSnapshot> result;
    std::optional<Vec2> playerCenter;
    std::size_t interiorOrdinal{};
    for (RaidMapAnchorRequest request : effectiveAnchorRequests(anchors))
    {
        if (request.allowedDistrictKinds.empty())
        {
            const auto constraint = definition.anchorDistrictKinds.find(request.id);
            if (constraint != definition.anchorDistrictKinds.end())
                request.allowedDistrictKinds = constraint->second;
        }
        const RaidLandmarkPlacementSnapshot *requiredLandmark{};
        if (!request.landmarkDefinitionId.empty())
        {
            const auto found = std::find_if(
                landmarks.begin(), landmarks.end(),
                [&](const RaidLandmarkPlacementSnapshot &landmark)
                {
                    return landmark.definitionId ==
                        request.landmarkDefinitionId;
                });
            if (found == landmarks.end())
                return {};
            requiredLandmark = &*found;
        }
        std::optional<Cell> selected;
        float selectedScore = request.kind == RaidMapAnchorKind::NormalExtraction
            ? -1.0F : std::numeric_limits<float>::infinity();
        if (request.kind == RaidMapAnchorKind::InteriorEntrance &&
            !landmarks.empty())
        {
            const RaidLandmarkPlacementSnapshot &landmark = requiredLandmark ? *requiredLandmark : landmarks.front();
            const Vec2 socket = requiredLandmark ? landmark.roadSockets.front()
                : landmark.roadSockets[interiorOrdinal % landmark.roadSockets.size()];
            selected = fineCellForPoint(map, socket);
            ++interiorOrdinal;
        }
        if (playerApproach && request.kind == RaidMapAnchorKind::PlayerSpawn)
            selected = fineCellForPoint(map, *playerApproach);
        const std::size_t candidateOffset = roadCandidates.empty()
            ? 0U
            : random.bounded(static_cast<std::uint32_t>(
                  roadCandidates.size()));
        const bool fixedSocket = usesHospitalOutdoorPlan(map) && selected.has_value();
        for (std::size_t probe{}; !fixedSocket && probe < roadCandidates.size(); ++probe)
        {
            const Cell candidate = roadCandidates[
                (probe + candidateOffset) % roadCandidates.size()];
            const std::uint16_t district = districtAtFineCell(
                definition, districtField, candidate.column, candidate.row);
            if (district >= districtDefinitions.size())
                continue;
            const RaidDistrictKind districtKind =
                districtDefinitions[district].kind;
            if ((!request.allowedDistrictKinds.empty() &&
                 std::find(request.allowedDistrictKinds.begin(),
                           request.allowedDistrictKinds.end(),
                           districtKind) == request.allowedDistrictKinds.end()) ||
                (request.allowedDistrictKinds.empty() &&
                 !districtKindMatchesAnchor(districtKind, request.kind)))
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
            if (bounds.position.x < map.walkableBounds.position.x ||
                bounds.position.y < map.walkableBounds.position.y ||
                bounds.position.x + bounds.size.x >
                    map.walkableBounds.position.x +
                        map.walkableBounds.size.x ||
                bounds.position.y + bounds.size.y >
                    map.walkableBounds.position.y +
                        map.walkableBounds.size.y)
            {
                continue;
            }
            if (request.minimumPlayerSpawnDistance > 0.0F &&
                playerCenter.has_value())
            {
                const float radius = request.minimumPlayerSpawnDistance;
                const ContentRect playerExclusion{
                    {playerCenter->x - radius, playerCenter->y - radius},
                    {radius * 2.0F, radius * 2.0F}};
                if (overlaps(playerExclusion, bounds))
                {
                    continue;
                }
            }
            // Landmark structures are mandatory colliders appended after
            // anchor placement. Unlike random props they do not pass through
            // the later occupied-cell check, so reserve them explicitly for
            // the new route without moving any preceding accepted anchor.
            if ((request.kind == RaidMapAnchorKind::CredentialExtraction || usesHospitalOutdoorPlan(map)) &&
                std::any_of(landmarks.begin(), landmarks.end(),
                    [&](const RaidLandmarkPlacementSnapshot &landmark)
                    {
                        return std::any_of(landmark.structures.begin(),
                            landmark.structures.end(), [&](ContentRect structure)
                            { return overlaps(inflated(structure, 80.0F), bounds); });
                    }))
            {
                continue;
            }
            if (requiredLandmark != nullptr)
            {
                const ContentRect vicinity = inflated(
                    requiredLandmark->bounds,
                    std::max(cellWidth, cellHeight) * 3.0F);
                const bool nearLandmark = center.x >= vicinity.position.x &&
                    center.y >= vicinity.position.y &&
                    center.x <= vicinity.position.x + vicinity.size.x &&
                    center.y <= vicinity.position.y + vicinity.size.y;
                if (!nearLandmark ||
                    district + 1U != requiredLandmark->districtInstanceId)
                {
                    continue;
                }
                if (request.kind == RaidMapAnchorKind::Enemy &&
                    overlaps(inflated(requiredLandmark->bounds, 20.0F),
                             bounds))
                {
                    continue;
                }
            }
            if (std::any_of(protectedDoorSockets.begin(), protectedDoorSockets.end(),
                    [&](Vec2 point) { return overlaps(pointRegion(point, 155.0F), bounds); }))
                continue;
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
    return raidDistrictFill(kind).terrain;
}

std::vector<RaidTerrainSpan> generateTerrain(
    const ProceduralOutdoorDefinition &definition,
    const DistrictField &districtField,
    const std::vector<RaidDistrictArchetypeDefinition> &districtDefinitions,
    const RoadSkeleton &roads,
    Pcg32 &random,
    std::optional<std::uint64_t> hospitalSeed)
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

    // Sample once per cell so water, soil and vegetation form coherent patches.
    // Roads always override the field and keep their authored cross-section.
    std::vector<RaidTerrainKind> ground;
    if (hospitalSeed)
    {
        ground.resize(roads.ranks.size());
        for (std::uint32_t row = 0; row < definition.rows; ++row)
            for (std::uint32_t column = 0; column < definition.columns; ++column)
            {
                auto index = cellIndex(definition, column, row);
                auto theme = districtDefinitions[districtAtFineCell(definition, districtField, column, row)].kind;
                int moisture = hospitalEnvironmentalField(*hospitalSeed, column, row);
                auto material = baseTerrainForDistrict(theme);
                if (theme == RaidDistrictKind::Greenbelt || theme == RaidDistrictKind::OpenGround)
                    material = moisture > 109 ? RaidTerrainKind::Grass : RaidTerrainKind::Dirt;
                else if (theme != RaidDistrictKind::Highway && moisture > 164)
                    material = RaidTerrainKind::Dirt;
                if (moisture > 193 || (moisture > 125 && puddles[index]))
                    material = RaidTerrainKind::Puddle;
                ground[index] = roads.ranks[index] ? RaidTerrainKind::Asphalt : material;
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
            if (hospitalSeed)
                kind = ground[cellIndex(definition, column, row)];
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
                if (hospitalSeed)
                    nextKind = ground[cellIndex(definition, column, row)];
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
    RaidGeneratedMapLayout &layout,
    std::uint64_t environmentSeed)
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
    const auto rectIsAvailable = [&](
                                     ContentRect rect,
                                     bool mustAvoidRoads,
                                     std::optional<std::uint16_t>
                                         requiredDistrict)
    {
        const float mapRight = map.walkableBounds.position.x +
            map.walkableBounds.size.x;
        const float mapBottom = map.walkableBounds.position.y +
            map.walkableBounds.size.y;
        if (rect.size.x <= 0.0F || rect.size.y <= 0.0F ||
            rect.position.x < map.walkableBounds.position.x ||
            rect.position.y < map.walkableBounds.position.y ||
            rect.position.x + rect.size.x > mapRight ||
            rect.position.y + rect.size.y > mapBottom)
        {
            return false;
        }
        const Cell first = fineCellForPoint(map, rect.position);
        const Cell last = fineCellForPoint(
            map, {rect.position.x + rect.size.x - 0.001F,
                  rect.position.y + rect.size.y - 0.001F});
        for (std::uint32_t row = first.row; row <= last.row; ++row)
        {
            for (std::uint32_t column = first.column;
                 column <= last.column; ++column)
            {
                const std::size_t index = cellIndex(
                    definition, column, row);
                if (occupied[index] != 0U ||
                    (mustAvoidRoads && roads.ranks[index] != 0U) ||
                    (requiredDistrict.has_value() &&
                     districtAtFineCell(
                         definition, districtField, column, row) !=
                         *requiredDistrict))
                {
                    return false;
                }
            }
        }
        return true;
    };
    const auto orientedSize = [](Vec2 longitudinal,
                                 std::uint8_t quarterTurns) noexcept
    {
        return quarterTurns % 2U == 0U
            ? longitudinal
            : Vec2{longitudinal.y, longitudinal.x};
    };
    const auto centeredBounds = [](Vec2 center, Vec2 size) noexcept
    {
        return ContentRect{
            {center.x - size.x * 0.5F, center.y - size.y * 0.5F},
            size};
    };
    const auto appendReservedProp = [&](
                                        RaidOutdoorPropKind kind,
                                        RaidOutdoorPropState state,
                                        ContentRect bounds,
                                        std::uint8_t quarterTurns,
                                        bool collidable)
    {
        appendProp(
            layout, kind, state, bounds, quarterTurns, collidable);
        markRect(bounds);
    };
    for (const RaidAnchorPlacementSnapshot &anchor : layout.anchorPlacements)
    {
        markRect(inflated(anchor.bounds, 80.0F));
        if (layout.layoutVersion == kHospitalLandmarkInteriorLayoutVersion &&
            anchor.kind == RaidMapAnchorKind::InteriorEntrance)
            markRect(pointRegion(raidExteriorReturnPoint(map, anchor.bounds), 50.0F));
    }
    for (const RaidLandmarkPlacementSnapshot &landmark : layout.landmarks)
    {
        markRect(landmark.bounds);
        for (const ContentRect &structure : landmark.structures)
            appendProp(layout, RaidOutdoorPropKind::Factory,
                       RaidOutdoorPropState::Weathered,
                       structure, 0U, true);
    }

    const bool hospital = usesHospitalOutdoorPlan(map);
    if (hospital)
    {
        std::array<unsigned, 3> familyCounts{};
        const auto plots = hospitalParcelCandidates(map, roads.ranks, environmentSeed);
        for (const auto &plot : plots)
        {
            auto &count = familyCounts[static_cast<std::size_t>(plot.family)];
            if (count >= 7 || !rectIsAvailable(plot.bounds, true, std::nullopt))
                continue;
            const Cell gate = fineCellForPoint(map, plot.gate);
            const auto district = districtAtFineCell(definition, districtField, gate.column, gate.row);
            if (districtDefinitions[district].kind == RaidDistrictKind::Greenbelt)
                continue;
            for (const auto &part : plot.parts)
                appendProp(layout, part.kind, part.state, part.bounds, part.quarterTurns, part.collidable);
            markRect(plot.bounds);
            ++count;
            if (familyCounts[0] == 7 && familyCounts[1] == 7 && familyCounts[2] == 7)
                break;
        }
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
            if (hospital && roads.ranks[cellIndex(definition, column, row)] == 0U)
            {
                // Park on the verge. The carriageway stays open across every crossing.
                openCells.push_back({column, row});
                if (column >= 2 && row >= 2 && column + 2 < definition.columns && row + 2 < definition.rows &&
                    (roads.ranks[cellIndex(definition, column - 2, row)] >= 2 ||
                     roads.ranks[cellIndex(definition, column + 2, row)] >= 2 ||
                     roads.ranks[cellIndex(definition, column, row - 2)] >= 2 ||
                     roads.ranks[cellIndex(definition, column, row + 2)] >= 2))
                    roadCells.push_back({column, row});
            }
            else if (!hospital && roads.ranks[cellIndex(definition, column, row)] >=
                roadRank(RaidOutdoorRoadKind::Secondary))
                roadCells.push_back({column, row});
            else if (roads.ranks[cellIndex(
                         definition, column, row)] == 0U)
                openCells.push_back({column, row});
        }
    }
    for (std::size_t remaining = roadCells.size(); remaining > 1U;
         --remaining)
    {
        const std::size_t selected = random.bounded(
            static_cast<std::uint32_t>(remaining));
        std::swap(roadCells[selected], roadCells[remaining - 1U]);
    }
    const std::uint32_t roadObstacleTarget = randomBetween(
        random, definition.minimumRoadObstacles,
        definition.maximumRoadObstacles);
    std::uint32_t roadObstaclesPlaced{};
    for (const Cell cell : roadCells)
    {
        if (roadObstaclesPlaced >= roadObstacleTarget)
            break;
        const bool barrier = roadObstaclesPlaced % 7U == 0U;
        const bool truck = !barrier && roadObstaclesPlaced % 4U == 0U;
        const Vec2 baseSize = barrier
            ? Vec2{176.0F, 34.0F}
            : (truck ? Vec2{432.0F, 152.0F}
                     : Vec2{272.0F, 108.0F});
        const auto roadAt = [&](int column, int row) noexcept
        {
            return column >= 0 && row >= 0 &&
                column < static_cast<int>(definition.columns) &&
                row < static_cast<int>(definition.rows) &&
                roads.ranks[cellIndex(
                    definition,
                    static_cast<std::uint32_t>(column),
                    static_cast<std::uint32_t>(row))] != 0U;
        };
        const int horizontalNeighbors =
            static_cast<int>(roadAt(
                static_cast<int>(cell.column) - 1,
                static_cast<int>(cell.row))) +
            static_cast<int>(roadAt(
                static_cast<int>(cell.column) + 1,
                static_cast<int>(cell.row)));
        const int verticalNeighbors =
            static_cast<int>(roadAt(
                static_cast<int>(cell.column),
                static_cast<int>(cell.row) - 1)) +
            static_cast<int>(roadAt(
                static_cast<int>(cell.column),
                static_cast<int>(cell.row) + 1));
        const bool vertical = verticalNeighbors > horizontalNeighbors ||
            (verticalNeighbors == horizontalNeighbors &&
             random.bounded(2U) == 1U);
        const std::uint8_t quarterTurns = static_cast<std::uint8_t>(
            (vertical ? 1U : 0U) + random.bounded(2U) * 2U);
        const Vec2 size = orientedSize(baseSize, quarterTurns);
        const Vec2 center{
            map.walkableBounds.position.x +
                (static_cast<float>(cell.column) + 0.5F) * cellWidth,
            map.walkableBounds.position.y +
                (static_cast<float>(cell.row) + 0.5F) * cellHeight};
        const ContentRect bounds = centeredBounds(center, size);
        if (!rectIsAvailable(bounds, hospital, std::nullopt))
            continue;
        appendReservedProp(
            barrier ? RaidOutdoorPropKind::RoadBarrier
                    : (truck ? RaidOutdoorPropKind::Truck
                             : RaidOutdoorPropKind::Car),
            roadObstaclesPlaced % 3U == 0U
                ? RaidOutdoorPropState::Damaged
                : RaidOutdoorPropState::Abandoned,
            bounds, quarterTurns, true);
        ++roadObstaclesPlaced;
    }

    const std::uint32_t blockerTarget = randomBetween(
        random, definition.minimumBlockers, definition.maximumBlockers);
    const std::uint32_t largeBuildingTarget = hospital ? 0U : randomBetween(random, 18U, 28U);
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
    const std::uint32_t compoundColumnPhase = 2U + random.bounded(8U);
    const std::uint32_t compoundRowPhase = 2U + random.bounded(6U);
    for (std::uint32_t row = compoundRowPhase;
         row + 12U < definition.rows; row += 14U)
        for (std::uint32_t column = compoundColumnPhase;
             column + 18U < definition.columns; column += 18U)
        {
            const std::uint16_t district = districtAtFineCell(
                definition, districtField, column, row);
            const RaidDistrictKind kind = districtDefinitions[district].kind;
            if (raidDistrictFill(kind).largeBuildings)
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
        const RaidDistrictFill fill = raidDistrictFill(districtKind);
        std::uint32_t widthCells = fill.minimumWidthCells +
            random.bounded(fill.widthVariation);
        std::uint32_t heightCells = fill.minimumHeightCells +
            random.bounded(fill.heightVariation);
        const std::uint8_t quarterTurns = static_cast<std::uint8_t>(
            random.bounded(2U));
        if (quarterTurns % 2U != 0U)
            std::swap(widthCells, heightCells);
        const ContentRect bounds{
            {map.walkableBounds.position.x + origin.column * cellWidth + 16.0F,
             map.walkableBounds.position.y + origin.row * cellHeight + 16.0F},
            {widthCells * cellWidth - 32.0F,
             heightCells * cellHeight - 32.0F}};
        if (!rectIsAvailable(bounds, true, district))
            continue;
        appendReservedProp(
            fill.building,
            RaidOutdoorPropState::Weathered,
            bounds,
            quarterTurns,
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
        const std::uint16_t district = districtAtFineCell(
            definition, districtField, origin.column, origin.row);
        const RaidDistrictKind districtKind =
            districtDefinitions[district].kind;
        if ((districtKind == RaidDistrictKind::Greenbelt ||
             districtKind == RaidDistrictKind::OpenGround) &&
            random.bounded(4U) != 0U)
            continue;
        const RaidOutdoorPropKind kind = raidDistrictFill(districtKind)
            .smallBlocker(layout.ballisticBlockers.size());
        const std::uint8_t quarterTurns = static_cast<std::uint8_t>(
            random.bounded(4U));
        const Vec2 size = orientedSize(
            kind == RaidOutdoorPropKind::Container
                ? Vec2{288.0F, 112.0F}
                : Vec2{256.0F, 160.0F},
            quarterTurns);
        const Vec2 center{
            map.walkableBounds.position.x +
                (static_cast<float>(origin.column) + 0.5F) * cellWidth,
            map.walkableBounds.position.y +
                (static_cast<float>(origin.row) + 0.5F) * cellHeight};
        const ContentRect bounds = centeredBounds(center, size);
        if (!rectIsAvailable(bounds, true, district))
            continue;
        appendReservedProp(
            kind, RaidOutdoorPropState::Weathered,
            bounds, quarterTurns, true);
    }

    const std::uint32_t decorativeTarget = randomBetween(
        random, definition.minimumDecorativeProps,
        definition.maximumDecorativeProps);
    std::uint32_t decorativePlaced{};
    for (std::uint32_t attempt{};
         decorativePlaced < decorativeTarget && !openCells.empty() &&
         attempt < decorativeTarget * 24U;
         ++attempt)
    {
        const Cell cell = openCells[random.bounded(
            static_cast<std::uint32_t>(openCells.size()))];
        if (hospital && hospitalEnvironmentalField(environmentSeed, cell.column, cell.row) < 95 &&
            random.bounded(4U) != 0U)
            continue;
        const Vec2 center{
            map.walkableBounds.position.x +
                (static_cast<float>(cell.column) + 0.2F +
                 static_cast<float>(random.bounded(60U)) / 100.0F) *
                    cellWidth,
            map.walkableBounds.position.y +
                (static_cast<float>(cell.row) + 0.2F +
                 static_cast<float>(random.bounded(60U)) / 100.0F) *
                    cellHeight};
        if (decorativePlaced % 8U == 0U)
        {
            const std::uint16_t district = districtAtFineCell(
                definition, districtField, cell.column, cell.row);
            const RaidDistrictKind kind = districtDefinitions[district].kind;
            if (kind == RaidDistrictKind::Industrial ||
                kind == RaidDistrictKind::Logistics ||
                kind == RaidDistrictKind::RoadsideService)
            {
                const std::uint8_t quarterTurns =
                    static_cast<std::uint8_t>(random.bounded(4U));
                const ContentRect equipmentBounds = centeredBounds(
                    center,
                    orientedSize({224.0F, 144.0F}, quarterTurns));
                if (rectIsAvailable(equipmentBounds, true, district))
                {
                    appendReservedProp(
                        RaidOutdoorPropKind::EngineeringEquipment,
                        RaidOutdoorPropState::Weathered,
                        equipmentBounds, quarterTurns, false);
                    ++decorativePlaced;
                    continue;
                }
            }
        }
        const float debrisSize =
            10.0F + static_cast<float>(random.bounded(20U));
        const ContentRect debrisBounds = centeredBounds(
            center, {debrisSize, debrisSize});
        if (!rectIsAvailable(debrisBounds, false, std::nullopt))
            continue;
        appendReservedProp(
            RaidOutdoorPropKind::Debris,
            RaidOutdoorPropState::Weathered,
            debrisBounds,
            static_cast<std::uint8_t>(random.bounded(4U)), false);
        ++decorativePlaced;
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

RaidGeneratedMapLayout generateV3Layout(const MapDefinition &map, std::uint64_t raidSeed,
                                        const RaidMapGenerationAnchors &anchors,
                                        std::uint32_t attempt, bool traceNetwork = false)
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
    DistrictField districtField;
    RoadSkeleton roads;
    std::vector<RaidLandmarkPlacementSnapshot> landmarks;
    std::optional<Vec2> playerApproach;
    if (usesHospitalOutdoorPlan(map))
    {
        auto plan = planHospitalOutdoor(map, definition.districtArchetypes, raidSeed, attempt);
        playerApproach = plan.playerApproach;
        districtField.owners = std::move(plan.owners);
        roads.ranks = std::move(plan.roadRanks);
        landmarks = std::move(plan.landmarks);
    }
    else
    {
        districtField = generateDistrictField(definition, districts, districtRandom);
        roads = buildV3Roads(definition, districtField, roadRandom, traceNetwork);
        landmarks = placeLandmarks(map, districtField, districts, roads, landmarkRandom);
    }
    if (landmarks.size() != definition.landmarkTemplates.size())
        return {};
    for (const RaidLandmarkPlacementSnapshot &landmark : landmarks)
        for (Vec2 socket : landmark.roadSockets)
            connectPointToRoad(map, roads, socket, RaidOutdoorRoadKind::Access);
    const std::vector<RaidAnchorPlacementSnapshot> placements = placeAnchors(
        map, districtField, districts, roads, landmarks, anchors, anchorRandom, playerApproach);
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
    layout.roadNetwork = freezeRoadNetwork(roads.segments);
    layout.anchorPlacements = placements;
    layout.landmarks = std::move(landmarks);
    layout.terrainSpans = generateTerrain(
        definition, districtField, districts, roads, terrainRandom,
        usesHospitalOutdoorPlan(map) ? std::optional<std::uint64_t>(raidSeed ^ attempt) : std::nullopt);
    populateProps(
        map, districtField, districts, roads, propRandom, layout, raidSeed ^ attempt);
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

std::optional<RaidAnchorPlacementSnapshot> selectHospitalRescueAnchor(
    const MapDefinition &map, const RaidGeneratedMapLayout &layout, Vec2 size)
{
    const auto *spawn = findRaidAnchorPlacement(layout, kRaidAnchorPlayerSpawn);
    if (spawn == nullptr || !finitePositive(size))
        return std::nullopt;
    const auto reachable = reachableLayoutCells(map, layout, spawn->bounds.position);
    if (reachable.empty())
        return std::nullopt;

    const auto &definition = map.proceduralOutdoor;
    const auto &walkable = map.walkableBounds;
    const float cellWidth = walkable.size.x / static_cast<float>(definition.columns);
    const float cellHeight = walkable.size.y / static_cast<float>(definition.rows);
    for (std::string_view districtId : {kHospitalRescueParkingDistrict, kHospitalRescueCampusDistrict})
    {
        const auto district = std::find_if(layout.districts.begin(), layout.districts.end(),
            [districtId](const RaidDistrictSnapshot &entry) {
                return isHospitalRescueDistrictInstance(entry.definitionId, districtId);
            });
        if (district == layout.districts.end())
            continue;

        std::optional<ContentRect> selected;
        float nearest = std::numeric_limits<float>::infinity();
        for (std::uint32_t row = 0; row < definition.rows; ++row)
        {
            const std::uint32_t districtRow = row * definition.districtRows / definition.rows;
            for (const RaidGridSpan &span : district->cells)
            {
                if (span.row != districtRow)
                    continue;
                const auto firstColumn = (span.firstColumn * definition.columns +
                    definition.districtColumns - 1U) / definition.districtColumns;
                const auto endColumn = ((span.firstColumn + span.length) * definition.columns +
                    definition.districtColumns - 1U) / definition.districtColumns;
                for (std::uint32_t column = firstColumn; column < endColumn; ++column)
                {
                    if (!reachable[cellIndex(definition, column, row)])
                        continue;
                    const Vec2 center{walkable.position.x + (column + 0.5F) * cellWidth,
                                      walkable.position.y + (row + 0.5F) * cellHeight};
                    const float dx = center.x - district->labelPosition.x;
                    const float dy = center.y - district->labelPosition.y;
                    const float distance = dx * dx + dy * dy;
                    if (distance >= nearest)
                        continue;

                    const ContentRect bounds{{center.x - size.x * 0.5F,
                                              center.y - size.y * 0.5F}, size};
                    const ContentRect clearance = inflated(bounds, 25.0F);
                    if (clearance.position.x < walkable.position.x ||
                        clearance.position.y < walkable.position.y ||
                        clearance.position.x + clearance.size.x > walkable.position.x + walkable.size.x ||
                        clearance.position.y + clearance.size.y > walkable.position.y + walkable.size.y ||
                        std::any_of(layout.ballisticBlockers.begin(), layout.ballisticBlockers.end(),
                            [&](ContentRect blocker) { return overlaps(clearance, blocker); }) ||
                        std::any_of(layout.anchorPlacements.begin(), layout.anchorPlacements.end(),
                            [&](const RaidAnchorPlacementSnapshot &anchor) {
                                return overlaps(bounds, inflated(anchor.bounds, 100.0F));
                            }))
                        continue;
                    selected = bounds;
                    nearest = distance;
                }
            }
        }
        if (selected)
            return RaidAnchorPlacementSnapshot{std::string{kRaidAnchorRescue}, RaidMapAnchorKind::Rescue,
                                              *selected, district->instanceId};
    }
    return std::nullopt;
}

namespace
{
bool clearEscortPassage(const MapDefinition &map, const RaidGeneratedMapLayout &layout, Vec2 from, Vec2 to)
{
    // Neighbours are axis aligned. The swept rectangle includes the whole character at either end.
    constexpr float halfWidth = 20.0F;
    const ContentRect swept{{std::min(from.x, to.x) - halfWidth, std::min(from.y, to.y) - halfWidth},
        {std::abs(from.x - to.x) + 2.0F * halfWidth, std::abs(from.y - to.y) + 2.0F * halfWidth}};
    const auto &bounds = map.walkableBounds;
    return swept.position.x >= bounds.position.x && swept.position.y >= bounds.position.y &&
        swept.position.x + swept.size.x <= bounds.position.x + bounds.size.x &&
        swept.position.y + swept.size.y <= bounds.position.y + bounds.size.y &&
        std::none_of(layout.ballisticBlockers.begin(), layout.ballisticBlockers.end(),
            [&](ContentRect obstacle) { return overlaps(swept, obstacle); });
}
}

bool validHospitalEscortStart(const MapDefinition &map, const RaidGeneratedMapLayout &layout,
                             ContentRect transfer, Vec2 start) noexcept
{
    const Vec2 center{transfer.position.x + transfer.size.x * 0.5F, transfer.position.y + transfer.size.y * 0.5F};
    const float distance = std::hypot(start.x - center.x, start.y - center.y);
    return distance >= 600.0F && distance <= 1000.0F && clearEscortPassage(map, layout, start, start);
}

std::optional<Vec2> selectHospitalEscortStart(const MapDefinition &map, const RaidGeneratedMapLayout &layout,
                                             ContentRect transfer)
{
    constexpr int radius = 14;
    constexpr int width = radius * 2 + 1;
    constexpr float stride = 80.0F;
    const Vec2 center{transfer.position.x + transfer.size.x * 0.5F, transfer.position.y + transfer.size.y * 0.5F};
    const auto position = [&](int x, int y) {
        return Vec2{center.x + stride * (x - radius), center.y + stride * (y - radius)};
    };
    if (!clearEscortPassage(map, layout, center, center)) return std::nullopt;
    std::array<bool, width * width> visited{};
    std::queue<std::pair<int, int>> frontier;
    frontier.emplace(radius, radius);
    visited[radius * width + radius] = true;
    constexpr std::array<std::pair<int, int>, 4> steps{{{1, 0}, {0, -1}, {-1, 0}, {0, 1}}};
    std::optional<Vec2> fallback;
    while (!frontier.empty())
    {
        const auto [x, y] = frontier.front();
        frontier.pop();
        const Vec2 from = position(x, y);
        if (validHospitalEscortStart(map, layout, transfer, from))
        {
            if (std::hypot(from.x - center.x, from.y - center.y) >= 720.0F) return from;
            fallback = from;
        }
        for (const auto &[dx, dy] : steps)
        {
            const int nx = x + dx, ny = y + dy;
            if (nx < 0 || nx >= width || ny < 0 || ny >= width) continue;
            const auto index = ny * width + nx;
            if (!visited[index] && clearEscortPassage(map, layout, from, position(nx, ny)))
            {
                visited[index] = true;
                frontier.emplace(nx, ny);
            }
        }
    }
    return fallback;
}

Vec2 raidResourcePointLootPosition(
    const RaidResourcePointSnapshot &resourcePoint,
    std::uint32_t slotIndex) noexcept
{
    const std::uint32_t capacity = std::max(1U, resourcePoint.capacity);
    const std::uint32_t columns = std::min(3U, capacity);
    const std::uint32_t rows = (capacity + columns - 1U) / columns;
    const std::uint32_t column = slotIndex % columns;
    const std::uint32_t row = slotIndex / columns;
    return {
        resourcePoint.bounds.position.x + resourcePoint.bounds.size.x *
            static_cast<float>(column + 1U) /
            static_cast<float>(columns + 1U),
        resourcePoint.bounds.position.y + resourcePoint.bounds.size.y *
            static_cast<float>(row + 1U) /
            static_cast<float>(rows + 1U)};
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
    const MapDefinition &map, std::uint64_t raidSeed, const RaidMapGenerationAnchors &anchors,
    bool newFrontierRoads, const std::function<bool(const RaidGeneratedMapLayout &)> &acceptRoads)
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

    // The optional credential exit must not change the attempt, prop stream,
    // or placement of any original anchor. Select it on the accepted roads.
    const auto optionalExit =
        std::find_if(anchors.requests.begin(), anchors.requests.end(), [](const auto &request) {
            return request.kind == RaidMapAnchorKind::CredentialExtraction;
        });
    if (newFrontierRoads && map.id == MapDefinitionId{"map.raid.frontier_exchange"} &&
        optionalExit != anchors.requests.end())
    {
        auto core = anchors;
        core.requests.erase(core.requests.begin() +
                            std::distance(anchors.requests.begin(), optionalExit));
        auto layout = generateRaidMapLayout(map, raidSeed, core, true, acceptRoads);
        const auto &def = map.proceduralOutdoor;
        Pcg32 choice{raidSeed, 0x657869742d7631ULL};
        const auto start = choice.bounded(static_cast<unsigned>(layout.roadCells.size()));
        const auto *spawn = findRaidAnchorPlacement(layout, kRaidAnchorPlayerSpawn);
        const auto reachable =
            reachableLayoutCells(map, layout, spawn ? spawn->bounds.position : anchors.playerSpawn);
        for (std::size_t offset = 0; offset < layout.roadCells.size(); ++offset)
        {
            const auto &cell = layout.roadCells[(start + offset) % layout.roadCells.size()];
            if (reachable.empty() || !reachable[cellIndex(def, cell.column, cell.row)])
                continue;
            const int col = cell.column * def.districtColumns / def.columns,
                      row = cell.row * def.districtRows / def.rows;
            const auto district =
                std::find_if(layout.districts.begin(), layout.districts.end(), [&](const auto &d) {
                    return districtKindMatchesAnchor(d.kind, optionalExit->kind) &&
                           std::any_of(d.cells.begin(), d.cells.end(), [&](const auto &span) {
                               return span.row == row && col >= span.firstColumn &&
                                      col < span.firstColumn + span.length;
                           });
                });
            if (district == layout.districts.end())
                continue;
            const Vec2 size = optionalExit->size;
            const ContentRect exit{
                {map.walkableBounds.position.x + (cell.column + 0.5F) * 80 - size.x / 2,
                 map.walkableBounds.position.y + (cell.row + 0.5F) * 80 - size.y / 2},
                size};
            if (exit.position.x < map.walkableBounds.position.x ||
                exit.position.y < map.walkableBounds.position.y ||
                exit.position.x + size.x >
                    map.walkableBounds.position.x + map.walkableBounds.size.x ||
                exit.position.y + size.y >
                    map.walkableBounds.position.y + map.walkableBounds.size.y)
                continue;
            const auto clearance = inflated(exit, 20);
            if (std::any_of(layout.ballisticBlockers.begin(), layout.ballisticBlockers.end(),
                            [&](auto r) { return overlaps(clearance, r); }) ||
                std::any_of(layout.anchorPlacements.begin(), layout.anchorPlacements.end(),
                            [&](const auto &a) { return overlaps(clearance, a.bounds); }) ||
                std::any_of(layout.landmarks.begin(), layout.landmarks.end(), [&](const auto &l) {
                    return std::any_of(l.structures.begin(), l.structures.end(),
                                       [&](auto r) { return overlaps(clearance, r); });
                }))
                continue;
            layout.anchorPlacements.push_back(
                {optionalExit->id, optionalExit->kind, exit, district->instanceId});
            if (!layout.roadNetwork.empty() && acceptRoads && !acceptRoads(layout))
            {
                layout.anchorPlacements.pop_back();
                continue;
            }
            layout.layoutHash = raidMapLayoutHash(layout);
            return layout;
        }
        return generateRaidMapLayout(map, raidSeed, anchors);
    }
    const ProceduralOutdoorDefinition &definition = map.proceduralOutdoor;
    if (definition.layoutVersion >= 3U)
    {
        for (std::uint32_t attempt = 1U;
             attempt <= definition.maximumAttempts;
             ++attempt)
        {
            RaidGeneratedMapLayout layout = generateV3Layout(
                map, raidSeed, anchors, attempt,
                newFrontierRoads && map.id == MapDefinitionId{"map.raid.frontier_exchange"} &&
                    map.walkableBounds.size.x == definition.columns * 80U &&
                    map.walkableBounds.size.y == definition.rows * 80U);
            if (layout.layoutVersion != definition.layoutVersion ||
                layout.districts.size() != (usesHospitalOutdoorPlan(map) ?
                    expandedDistricts(definition).size() : 8U) ||
                layout.landmarks.size() != (usesHospitalOutdoorPlan(map) ?
                    definition.landmarkTemplates.size() : 3U) ||
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
            if (layoutConnects(map, layout, frozenAnchors) &&
                (layout.roadNetwork.empty() || !acceptRoads || acceptRoads(layout)) &&
                (!usesHospitalOutdoorPlan(map) ||
                 inspectHospitalOutdoorRoutes(map, layout).passable()))
            {
                layout.layoutHash = raidMapLayoutHash(layout);
                return layout;
            }
        }

        if (newFrontierRoads && map.id == MapDefinitionId{"map.raid.frontier_exchange"})
            return generateRaidMapLayout(map, raidSeed, anchors);
        MapDefinition fallbackMap = map;
        // Preserve the accepted 320x180 fallback exactly, but do not pack the
        // full Frontier population of props into a smaller theme.
        const auto scaled = [&](std::uint32_t count) {
            return std::max(1U, static_cast<std::uint32_t>(
                static_cast<std::uint64_t>(count) * definition.columns * definition.rows / (320U * 180U)));
        };
        fallbackMap.proceduralOutdoor.minimumBlockers = scaled(700U);
        fallbackMap.proceduralOutdoor.maximumBlockers = scaled(700U);
        fallbackMap.proceduralOutdoor.minimumDecorativeProps = scaled(1200U);
        fallbackMap.proceduralOutdoor.maximumDecorativeProps = scaled(1200U);
        fallbackMap.proceduralOutdoor.minimumRoadObstacles = scaled(140U);
        fallbackMap.proceduralOutdoor.maximumRoadObstacles = scaled(140U);
        fallbackMap.proceduralOutdoor.minimumPuddlePatches = scaled(60U);
        fallbackMap.proceduralOutdoor.maximumPuddlePatches = scaled(60U);
        RaidGeneratedMapLayout fallback = generateV3Layout(
            fallbackMap,
            0x46524f4e54494552ULL,
            anchors,
            1U);
        if (usesHospitalOutdoorPlan(map))
        {
            for (unsigned attempt = 2; attempt <= 8 &&
                 (fallback.layoutVersion == 0U || !inspectHospitalOutdoorRoutes(map, fallback).passable()); ++attempt)
                fallback = generateV3Layout(fallbackMap, 0x46524f4e54494552ULL, anchors, attempt);
            if (fallback.layoutVersion == 0U || !inspectHospitalOutdoorRoutes(map, fallback).passable())
                throw std::runtime_error{"No safe hospital approach layout satisfies these anchors"};
        }
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

Vec2 raidExteriorReturnPoint(const MapDefinition &map, ContentRect entrance) noexcept
{
    return {entrance.position.x + entrance.size.x * 0.5F,
            std::min(map.walkableBounds.position.y + map.walkableBounds.size.y - 50.0F,
                     entrance.position.y + entrance.size.y + 70.0F)};
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
            layoutConnects(map, layout, frozenAnchors) &&
            (!isHospitalHybridLayoutVersion(layout.layoutVersion) || !usesHospitalOutdoorPlan(map) ||
             inspectHospitalOutdoorRoutes(map, layout).passable());
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
    if (!layout.roadNetwork.empty())
    {
        hashValue(hash, 0x524f4144U);
        hashValue(hash, static_cast<std::uint32_t>(layout.roadNetwork.size()));
        for (const auto &s : layout.roadNetwork)
            for (int v : {s.x1, s.y1, s.x2, s.y2, static_cast<int>(s.kind)})
                hashValue(hash, static_cast<std::uint32_t>(v));
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
    if (layout.layoutVersion >= 4U)
    {
        hashValue(hash, static_cast<std::uint32_t>(
            layout.resourcePoints.size()));
        for (const RaidResourcePointSnapshot &resourcePoint :
             layout.resourcePoints)
        {
            hashString(hash, resourcePoint.instanceId);
            hashString(hash, resourcePoint.definitionId);
            hashString(hash, resourcePoint.displayName);
            hashValue(hash, static_cast<std::uint32_t>(resourcePoint.kind));
            hashString(hash, resourcePoint.lootTableId.value());
            hashValue(hash, resourcePoint.riskTier);
            hashValue(hash, resourcePoint.capacity);
            hashRect(hash, resourcePoint.bounds);
            hashValue(hash, resourcePoint.districtInstanceId);
            hashString(hash, resourcePoint.landmarkDefinitionId);
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
