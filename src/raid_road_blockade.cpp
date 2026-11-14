#include "raid_map_generation.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <limits>
#include <vector>

#include "raid_visibility.h"
#include "stable_random.h"

namespace
{
constexpr float kPlayerHalfSize{16.0F}; // Player::size() is 32.
constexpr float kClearance{kPlayerHalfSize + 1.0F};
constexpr float kBlockadeDepth{64.0F};
constexpr float kMinimumCrossSection{400.0F};
constexpr float kMaximumRoadWidth{320.0F};
constexpr float kApproachDistance{100.0F};
constexpr float kMaximumFocusDistance{1440.0F};
constexpr float kAnchorBuffer{48.0F};
constexpr float kDistanceTolerance{0.01F};

Vec2 oppositeCorner(ContentRect bounds) noexcept
{
    return {bounds.position.x + bounds.size.x,
            bounds.position.y + bounds.size.y};
}

Vec2 center(ContentRect bounds) noexcept
{
    return {bounds.position.x + bounds.size.x * 0.5F,
            bounds.position.y + bounds.size.y * 0.5F};
}

ContentRect padded(ContentRect bounds, float amount) noexcept
{
    return {{bounds.position.x - amount, bounds.position.y - amount},
            {bounds.size.x + 2.0F * amount, bounds.size.y + 2.0F * amount}};
}

bool contains(ContentRect bounds, Vec2 point) noexcept
{
    const Vec2 maximum = oppositeCorner(bounds);
    return point.x >= bounds.position.x && point.x <= maximum.x &&
        point.y >= bounds.position.y && point.y <= maximum.y;
}

bool overlaps(ContentRect left, ContentRect right) noexcept
{
    const Vec2 leftMax = oppositeCorner(left);
    const Vec2 rightMax = oppositeCorner(right);
    return left.position.x < rightMax.x && leftMax.x > right.position.x &&
        left.position.y < rightMax.y && leftMax.y > right.position.y;
}

bool fits(ContentRect outer, ContentRect inner) noexcept
{
    return contains(outer, inner.position) &&
        contains(outer, oppositeCorner(inner));
}

float distance(Vec2 left, Vec2 right) noexcept
{
    return std::hypot(left.x - right.x, left.y - right.y);
}

Vec2 exteriorReturnCenter(const MapDefinition &map, ContentRect entrance) noexcept
{
    return {center(entrance).x + kPlayerHalfSize,
            std::min(oppositeCorner(map.walkableBounds).y - 50.0F,
                     oppositeCorner(entrance).y + 70.0F) + kPlayerHalfSize};
}

bool supportedMap(const MapDefinition &map, const RaidGeneratedMapLayout &layout)
{
    return map.id == MapDefinitionId{"map.raid.frontier_exchange"} &&
        map.proceduralOutdoor.enabled && map.proceduralOutdoor.rows > 0U &&
        map.proceduralOutdoor.columns > 0U && layout.layoutVersion >= 4U &&
        !layout.usedFallback;
}

std::vector<ContentRect> playerBlockers(const RaidGeneratedMapLayout &layout)
{
    std::vector<ContentRect> result;
    result.reserve(layout.ballisticBlockers.size() + 1U);
    for (const ContentRect bounds : layout.ballisticBlockers)
        result.push_back(padded(bounds, kClearance));
    return result;
}

bool segmentClear(Vec2 from, Vec2 to, const std::vector<ContentRect> &blockers)
{
    return std::none_of(blockers.begin(), blockers.end(), [&](ContentRect bounds)
    {
        return raidSightSegmentIntersectsRectInterior(
            from, to, {bounds.position, bounds.size});
    });
}

bool avoidsInteractionRegions(const MapDefinition &map,
                              const RaidGeneratedMapLayout &layout,
                              const ContentRect &crisisFocus,
                              ContentRect blockade)
{
    const ContentRect buffer = padded(blockade, kAnchorBuffer);
    if (overlaps(buffer, crisisFocus))
        return false;
    for (const RaidAnchorPlacementSnapshot &anchor : layout.anchorPlacements)
    {
        if (overlaps(buffer, anchor.bounds) ||
            (anchor.kind == RaidMapAnchorKind::InteriorEntrance &&
             contains(buffer, exteriorReturnCenter(map, anchor.bounds))))
            return false;
    }
    return std::none_of(map.fieldIntelligenceTradePositions.begin(),
                        map.fieldIntelligenceTradePositions.end(),
                        [&](Vec2 point) { return contains(buffer, point); });
}

// The entire road band must be continuous on both sides. A junction or a
// change in the marked road width is not a cross-section of one road.
std::vector<RaidRoadBlockadeSnapshot> roadCrossSections(
    const MapDefinition &map, const RaidGeneratedMapLayout &layout,
    const ContentRect &crisisFocus)
{
    const auto columns = static_cast<int>(map.proceduralOutdoor.columns);
    const auto rows = static_cast<int>(map.proceduralOutdoor.rows);
    const Vec2 cellSize{map.walkableBounds.size.x / static_cast<float>(columns),
                        map.walkableBounds.size.y / static_cast<float>(rows)};
    std::vector<int> road(static_cast<std::size_t>(columns) * rows, -1);
    for (const auto cell : layout.roadCells)
    {
        if (cell.column >= columns || cell.row >= rows)
            return {};
        road[static_cast<std::size_t>(cell.row) * columns + cell.column] =
            static_cast<int>(cell.kind);
    }
    const auto at = [&](int column, int row) noexcept
    {
        return column >= 0 && column < columns && row >= 0 && row < rows
            ? road[static_cast<std::size_t>(row) * columns + column] : -1;
    };
    const Vec2 focus = center(crisisFocus);
    std::vector<RaidRoadBlockadeSnapshot> result;
    for (const RaidOutdoorRoadCell cell : layout.roadCells)
    {
        const Vec2 cellCenter{
            map.walkableBounds.position.x + (cell.column + 0.5F) * cellSize.x,
            map.walkableBounds.position.y + (cell.row + 0.5F) * cellSize.y};
        if (cell.kind == RaidOutdoorRoadKind::Access ||
            distance(cellCenter, focus) > kMaximumFocusDistance)
            continue;
        for (const bool horizontal : {true, false})
        {
            const int rank = static_cast<int>(cell.kind);
            const int along = horizontal ? cell.column : cell.row;
            const int across = horizontal ? cell.row : cell.column;
            const auto inBand = [&](int forward, int lateral) noexcept
            {
                return (horizontal ? at(forward, lateral) : at(lateral, forward)) == rank;
            };
            if (inBand(along, across - 1))
                continue;
            int end = across;
            while (inBand(along, end + 1))
                ++end;
            const float lateralStep = horizontal ? cellSize.y : cellSize.x;
            const float roadWidth = static_cast<float>(end - across + 1) * lateralStep;
            if (roadWidth < 160.0F || roadWidth > kMaximumRoadWidth)
                continue;
            bool continuous = true;
            for (const int neighbor : {along - 1, along + 1})
            {
                continuous = continuous && !inBand(neighbor, across - 1) &&
                    !inBand(neighbor, end + 1);
                for (int lateral = across; lateral <= end; ++lateral)
                    continuous = continuous && inBand(neighbor, lateral);
            }
            if (!continuous)
                continue;
            const float centerOffset = lateralStep * 0.5F * static_cast<float>(end - across);
            const Vec2 midpoint{
                cellCenter.x + (horizontal ? 0.0F : centerOffset),
                cellCenter.y + (horizontal ? centerOffset : 0.0F)};
            if (distance(midpoint, focus) > kMaximumFocusDistance)
                continue;
            const Vec2 size{
                horizontal ? kBlockadeDepth : kMinimumCrossSection,
                horizontal ? kMinimumCrossSection : kBlockadeDepth};
            const Vec2 approach{horizontal ? kApproachDistance : 0.0F,
                                horizontal ? 0.0F : kApproachDistance};
            result.push_back({{{midpoint.x - size.x * 0.5F, midpoint.y - size.y * 0.5F},
                               size},
                              {midpoint.x - approach.x, midpoint.y - approach.y},
                              {midpoint.x + approach.x, midpoint.y + approach.y}});
        }
    }
    return result;
}

struct RectangleDetour
{
    float extraDistance{std::numeric_limits<float>::infinity()};
    std::array<Vec2, 2> corners{};
};

RectangleDetour shortestRectangleDetour(
    const RaidRoadBlockadeSnapshot &candidate, ContentRect obstacle,
    ContentRect walkable, const std::vector<ContentRect> &blockers)
{
    const Vec2 low = obstacle.position;
    const Vec2 high = oppositeCorner(obstacle);
    const bool horizontal = candidate.routeEntry.y == candidate.routeExit.y;
    const std::array<Vec2, 2> first{low, horizontal ? Vec2{low.x, high.y}
                                                            : Vec2{high.x, low.y}};
    const std::array<Vec2, 2> second{horizontal ? Vec2{high.x, low.y}
                                                 : Vec2{low.x, high.y}, high};
    RectangleDetour best;
    const float direct = distance(candidate.routeEntry, candidate.routeExit);
    for (std::size_t side{}; side < first.size(); ++side)
    {
        if (!contains(walkable, first[side]) || !contains(walkable, second[side]) ||
            !segmentClear(candidate.routeEntry, first[side], blockers) ||
            !segmentClear(first[side], second[side], blockers) ||
            !segmentClear(second[side], candidate.routeExit, blockers))
            continue;
        const float extraDistance = distance(candidate.routeEntry, first[side]) +
            distance(first[side], second[side]) +
            distance(second[side], candidate.routeExit) - direct;
        if (extraDistance < best.extraDistance - kDistanceTolerance)
            best = {extraDistance, {first[side], second[side]}};
    }
    return best;
}

bool certifyDetour(const MapDefinition &map, const RaidGeneratedMapLayout &layout,
                  const ContentRect &focus, const std::vector<ContentRect> &blockers,
                  RaidRoadBlockadeSnapshot &candidate)
{
    const ContentRect walkable = padded(map.walkableBounds, -kClearance);
    if (!fits(walkable, candidate.bounds) ||
        !contains(walkable, candidate.routeEntry) || !contains(walkable, candidate.routeExit) ||
        !avoidsInteractionRegions(map, layout, focus, candidate.bounds) ||
        std::any_of(layout.ballisticBlockers.begin(), layout.ballisticBlockers.end(),
                    [&](ContentRect bounds) { return overlaps(bounds, candidate.bounds); }) ||
        !segmentClear(candidate.routeEntry, candidate.routeExit, blockers))
        return false;

    // In open space the shortest trip around a rectangle passes its two
    // corners on one side. This gives a lower bound with the actual player
    // footprint, and a usable upper bound once the rest of the map and a
    // little extra clearance are included. No grid can inflate the detour.
    const float lower = shortestRectangleDetour(
        candidate, padded(candidate.bounds, kPlayerHalfSize), map.walkableBounds,
        {}).extraDistance;
    const RectangleDetour detour = shortestRectangleDetour(
        candidate, padded(candidate.bounds, kClearance), walkable, blockers);
    if (lower < kRaidRoadBlockadeMinimumDetour ||
        detour.extraDistance > kRaidRoadBlockadeMaximumDetour)
        return false;
    candidate.detourExtraDistance = detour.extraDistance;
    candidate.detourCorners = detour.corners;
    return true;
}

std::size_t axisIndex(const std::vector<float> &axis, float value)
{
    return static_cast<std::size_t>(
        std::distance(axis.begin(), std::lower_bound(axis.begin(), axis.end(), value)));
}

// Partition space at collider edges, rather than rounding to a grid. Each
// open rectangle is wholly clear for the player center, and two open regions
// sharing an edge give a traversable passage even through a narrow doorway.
bool preservesCriticalPaths(const MapDefinition &map,
                            const RaidGeneratedMapLayout &layout,
                            const ContentRect &focus,
                            const RaidRoadBlockadeSnapshot &candidate,
                            const std::vector<ContentRect> &blockers)
{
    const auto *signal = findRaidAnchorPlacement(layout, kRaidAnchorEmergencyExtraction);
    const auto *spawn = findRaidAnchorPlacement(layout, kRaidAnchorPlayerSpawn);
    if (signal == nullptr || spawn == nullptr ||
        findRaidAnchorPlacement(layout, kRaidAnchorNormalExtraction) == nullptr ||
        findRaidAnchorPlacement(layout, kRaidAnchorConditionalExtraction) == nullptr ||
        findRaidAnchorPlacement(layout, kRaidAnchorHighRiskControl) == nullptr)
        return false;
    const ContentRect walkable = padded(map.walkableBounds, -kClearance);
    const Vec2 minimum = walkable.position;
    const Vec2 maximum = oppositeCorner(walkable);
    std::vector<float> x{minimum.x, maximum.x};
    std::vector<float> y{minimum.y, maximum.y};
    std::vector<ContentRect> blocked = blockers;
    blocked.push_back(padded(candidate.bounds, kClearance));
    for (ContentRect &bounds : blocked)
    {
        const Vec2 right = oppositeCorner(bounds);
        const Vec2 low{std::clamp(bounds.position.x, minimum.x, maximum.x),
                       std::clamp(bounds.position.y, minimum.y, maximum.y)};
        const Vec2 high{std::clamp(right.x, minimum.x, maximum.x),
                        std::clamp(right.y, minimum.y, maximum.y)};
        bounds = {low, {high.x - low.x, high.y - low.y}};
        x.insert(x.end(), {low.x, high.x});
        y.insert(y.end(), {low.y, high.y});
    }
    for (auto *axis : {&x, &y})
    {
        std::sort(axis->begin(), axis->end());
        axis->erase(std::unique(axis->begin(), axis->end()), axis->end());
    }
    const std::size_t columns = x.size() - 1U;
    const std::size_t rows = y.size() - 1U;
    std::vector<std::uint8_t> cells(columns * rows, 0U);
    for (const ContentRect bounds : blocked)
    {
        const Vec2 high = oppositeCorner(bounds);
        const std::size_t left = axisIndex(x, bounds.position.x);
        const std::size_t right = axisIndex(x, high.x);
        const std::size_t top = axisIndex(y, bounds.position.y);
        const std::size_t bottom = axisIndex(y, high.y);
        for (std::size_t row = top; row < bottom; ++row)
            std::fill(cells.begin() + row * columns + left,
                      cells.begin() + row * columns + right, std::uint8_t{1});
    }
    const auto visitAround = [&](Vec2 point, const auto &visit)
    {
        if (!contains(walkable, point))
            return;
        const std::size_t right = axisIndex(x, point.x);
        const std::size_t bottom = axisIndex(y, point.y);
        for (std::size_t row = bottom > 0U ? bottom - 1U : 0U;
             row <= bottom && row < rows; ++row)
            for (std::size_t column = right > 0U ? right - 1U : 0U;
                 column <= right && column < columns; ++column)
                if (x[column] <= point.x && point.x <= x[column + 1U] &&
                    y[row] <= point.y && point.y <= y[row + 1U])
                    visit(row * columns + column);
    };
    std::vector<std::size_t> queue;
    const auto visit = [&](std::size_t index)
    {
        if (cells[index] == 0U)
        {
            cells[index] = 2U;
            queue.push_back(index);
        }
    };
    visitAround(center(signal->bounds), visit);
    for (std::size_t cursor{}; cursor < queue.size(); ++cursor)
    {
        const std::size_t index = queue[cursor];
        const std::size_t column = index % columns;
        if (column > 0U)
            visit(index - 1U);
        if (column + 1U < columns)
            visit(index + 1U);
        if (index >= columns)
            visit(index - columns);
        if (index + columns < cells.size())
            visit(index + columns);
    }
    const auto reachable = [&](Vec2 point)
    {
        bool visited = false;
        visitAround(point, [&](std::size_t index) { visited |= cells[index] == 2U; });
        return visited;
    };
    if (!reachable({spawn->bounds.position.x + kPlayerHalfSize,
                    spawn->bounds.position.y + kPlayerHalfSize}) ||
        !reachable(candidate.routeEntry) || !reachable(candidate.routeExit))
        return false;

    const auto relevant = [](RaidMapAnchorKind kind) noexcept
    {
        return kind != RaidMapAnchorKind::Enemy && kind != RaidMapAnchorKind::PressureSpawn;
    };
    for (const RaidAnchorPlacementSnapshot &anchor : layout.anchorPlacements)
    {
        if ((relevant(anchor.kind) && !reachable(center(anchor.bounds))) ||
            (anchor.kind == RaidMapAnchorKind::InteriorEntrance &&
             !reachable(exteriorReturnCenter(map, anchor.bounds))))
            return false;
    }
    // These are the exact positions used for the fixed high-risk loot slots.
    const std::size_t slots = map.highRisk.advancedLootSlots.size();
    for (std::size_t index{}; index < slots; ++index)
    {
        if (!reachable({focus.position.x + focus.size.x *
                            static_cast<float>(index + 1U) / static_cast<float>(slots + 1U),
                        center(focus).y}))
            return false;
    }
    return true;
}
}

std::optional<RaidRoadBlockadeSnapshot> chooseFrontierRoadBlockade(
    const MapDefinition &map, const RaidGeneratedMapLayout &layout,
    const ContentRect &crisisFocus, std::uint64_t seed)
{
    if (!supportedMap(map, layout))
        return std::nullopt;
    auto candidates = roadCrossSections(map, layout, crisisFocus);
    std::sort(candidates.begin(), candidates.end(), [](const auto &left, const auto &right)
    {
        if (left.bounds.position.y != right.bounds.position.y)
            return left.bounds.position.y < right.bounds.position.y;
        if (left.bounds.position.x != right.bounds.position.x)
            return left.bounds.position.x < right.bounds.position.x;
        return left.bounds.size.x < right.bounds.size.x;
    });
    Pcg32 random{seed, 0x626c6f636b616465ULL};
    for (std::size_t remaining = candidates.size(); remaining > 1U; --remaining)
    {
        const std::size_t selected = random.bounded(static_cast<std::uint32_t>(remaining));
        std::swap(candidates[selected], candidates[remaining - 1U]);
    }
    const auto blockers = playerBlockers(layout);
    for (auto &candidate : candidates)
    {
        if (certifyDetour(map, layout, crisisFocus, blockers, candidate) &&
            preservesCriticalPaths(map, layout, crisisFocus, candidate, blockers))
            return candidate;
    }
    return std::nullopt;
}

bool validateFrontierRoadBlockade(
    const MapDefinition &map, const RaidGeneratedMapLayout &layout,
    const ContentRect &crisisFocus, const RaidRoadBlockadeSnapshot &blockade)
{
    if (!supportedMap(map, layout))
        return false;
    auto candidates = roadCrossSections(map, layout, crisisFocus);
    const auto candidate = std::find_if(candidates.begin(), candidates.end(),
        [&](const RaidRoadBlockadeSnapshot &option) { return option.bounds == blockade.bounds; });
    if (candidate == candidates.end())
        return false;
    const auto blockers = playerBlockers(layout);
    if (!certifyDetour(map, layout, crisisFocus, blockers, *candidate) ||
        !std::isfinite(blockade.detourExtraDistance) ||
        blockade.detourExtraDistance < kRaidRoadBlockadeMinimumDetour ||
        blockade.detourExtraDistance > kRaidRoadBlockadeMaximumDetour ||
        std::abs(candidate->detourExtraDistance - blockade.detourExtraDistance) > kDistanceTolerance)
        return false;
    candidate->detourExtraDistance = blockade.detourExtraDistance;
    return *candidate == blockade &&
        preservesCriticalPaths(map, layout, crisisFocus, blockade, blockers);
}
