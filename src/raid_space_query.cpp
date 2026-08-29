#include "raid_space_query.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <iterator>
#include <limits>
#include <queue>
#include <vector>

namespace
{
    constexpr float kGeometryEpsilon{0.001F};
    constexpr float kDistanceEpsilon{0.0001F};
    constexpr std::size_t kNoNode =
        std::numeric_limits<std::size_t>::max();
    constexpr std::size_t kDenseGridBlockerThreshold{48U};
    constexpr float kDenseNavigationGridCellSize{48.0F};
    constexpr int kDenseGridNeighborOffsets[4][2]{
        {0, -1}, {-1, 0}, {1, 0}, {0, 1}};

    bool finite(Vec2 value) noexcept
    {
        return std::isfinite(value.x) && std::isfinite(value.y);
    }

    bool finitePositive(Vec2 value) noexcept
    {
        return finite(value) && value.x > 0.0F && value.y > 0.0F;
    }

    float distance(Vec2 first, Vec2 second) noexcept
    {
        const float deltaX = second.x - first.x;
        const float deltaY = second.y - first.y;
        const float squared = deltaX * deltaX + deltaY * deltaY;
        return std::isfinite(squared)
            ? std::sqrt(squared)
            : std::numeric_limits<float>::infinity();
    }

    bool pointInsideOpenRect(Vec2 point, const Rect &rect) noexcept
    {
        return point.x > rect.position.x + kGeometryEpsilon &&
               point.x < rect.position.x + rect.size.x - kGeometryEpsilon &&
               point.y > rect.position.y + kGeometryEpsilon &&
               point.y < rect.position.y + rect.size.y - kGeometryEpsilon;
    }

    bool segmentIntersectsRectInterior(
        Vec2 start,
        Vec2 end,
        const Rect &rect) noexcept
    {
        if (!finite(start) || !finite(end) || !finite(rect.position) ||
            !finitePositive(rect.size))
        {
            return true;
        }

        const float lowerX = rect.position.x + kGeometryEpsilon;
        const float upperX = rect.position.x + rect.size.x - kGeometryEpsilon;
        const float lowerY = rect.position.y + kGeometryEpsilon;
        const float upperY = rect.position.y + rect.size.y - kGeometryEpsilon;
        if (lowerX >= upperX || lowerY >= upperY)
        {
            return false;
        }

        const Vec2 delta{end.x - start.x, end.y - start.y};
        float enter{};
        float leave{1.0F};
        const auto clipAxis = [&](float origin, float movement,
                                  float lower, float upper)
        {
            if (std::abs(movement) <= kGeometryEpsilon)
            {
                return origin > lower && origin < upper;
            }

            float first = (lower - origin) / movement;
            float second = (upper - origin) / movement;
            if (first > second)
            {
                std::swap(first, second);
            }
            enter = std::max(enter, first);
            leave = std::min(leave, second);
            return enter <= leave;
        };

        if (!clipAxis(start.x, delta.x, lowerX, upperX) ||
            !clipAxis(start.y, delta.y, lowerY, upperY))
        {
            return false;
        }
        return leave >= 0.0F && enter <= 1.0F &&
               leave - enter > kGeometryEpsilon;
    }

    Rect expandedForActor(
        const Rect &rect,
        Vec2 actorSize,
        float clearance) noexcept
    {
        const Vec2 expansion{
            actorSize.x * 0.5F + clearance,
            actorSize.y * 0.5F + clearance};
        return Rect{
            Vec2{
                rect.position.x - expansion.x,
                rect.position.y - expansion.y},
            Vec2{
                rect.size.x + expansion.x * 2.0F,
                rect.size.y + expansion.y * 2.0F}};
    }

    bool pointInsideWorldForActor(
        Vec2 point,
        Vec2 actorSize,
        Vec2 worldSize) noexcept
    {
        const Vec2 half{actorSize.x * 0.5F, actorSize.y * 0.5F};
        return point.x >= half.x && point.y >= half.y &&
               point.x <= worldSize.x - half.x &&
               point.y <= worldSize.y - half.y;
    }

}

bool raidSpaceHasLineOfSight(
    Vec2 start,
    Vec2 end,
    std::span<const BallisticBlocker> blockers) noexcept
{
    if (!finite(start) || !finite(end))
    {
        return false;
    }

    return std::none_of(
        blockers.begin(),
        blockers.end(),
            [&](const BallisticBlocker &blocker)
            {
                return raidSpaceSegmentIntersectsBlockerInterior(
                    start, end, blocker.bounds);
            });
}

bool raidSpaceSegmentIntersectsBlockerInterior(
    Vec2 start,
    Vec2 end,
    Rect blocker) noexcept
{
    return segmentIntersectsRectInterior(start, end, blocker);
}

std::optional<RaidSpaceNavigationField>
RaidSpaceNavigationField::build(
    Vec2 actorSize,
    Vec2 worldSize,
    std::span<const BallisticBlocker> blockers,
    float clearance)
{
    if (!finitePositive(actorSize) || !finitePositive(worldSize) ||
        !std::isfinite(clearance) || clearance < 0.0F)
    {
        return std::nullopt;
    }

    RaidSpaceNavigationField field;
    field.actorSize_ = actorSize;
    field.worldSize_ = worldSize;
    field.clearance_ = clearance;
    field.expandedBlockers_.reserve(blockers.size());
    for (const BallisticBlocker &blocker : blockers)
    {
        if (!finite(blocker.bounds.position) ||
            !finitePositive(blocker.bounds.size))
        {
            return std::nullopt;
        }
        field.expandedBlockers_.push_back(
            expandedForActor(blocker.bounds, actorSize, clearance));
    }
    std::vector<BallisticBlocker> expandedBlockers;
    expandedBlockers.reserve(field.expandedBlockers_.size());
    for (std::size_t index{}; index < field.expandedBlockers_.size(); ++index)
    {
        expandedBlockers.push_back(BallisticBlocker{
            static_cast<std::uint32_t>(index + 1U),
            field.expandedBlockers_[index]});
    }
    field.expandedBlockerIndex_ = RaidSpaceBlockerIndex::build(
        worldSize,
        expandedBlockers);
    if (!field.expandedBlockerIndex_.has_value())
    {
        return std::nullopt;
    }

    if (field.expandedBlockers_.size() >= kDenseGridBlockerThreshold)
    {
        const Vec2 halfActor{actorSize.x * 0.5F, actorSize.y * 0.5F};
        const Vec2 navigableSize{
            worldSize.x - actorSize.x,
            worldSize.y - actorSize.y};
        if (navigableSize.x < 0.0F || navigableSize.y < 0.0F)
        {
            return std::nullopt;
        }
        field.usesDenseGrid_ = true;
        field.denseGridCellSize_ = kDenseNavigationGridCellSize;
        field.denseGridOrigin_ = halfActor;
        field.denseGridColumns_ = static_cast<std::size_t>(
            std::floor(navigableSize.x / kDenseNavigationGridCellSize)) + 1U;
        field.denseGridRows_ = static_cast<std::size_t>(
            std::floor(navigableSize.y / kDenseNavigationGridCellSize)) + 1U;
        const std::size_t gridSize =
            field.denseGridColumns_ * field.denseGridRows_;
        field.denseGridWalkable_.assign(gridSize, 1U);
        std::vector<std::size_t> blockerCandidates;
        for (std::size_t row{}; row < field.denseGridRows_; ++row)
        {
            for (std::size_t column{};
                 column < field.denseGridColumns_;
                 ++column)
            {
                const Vec2 point{
                    field.denseGridOrigin_.x +
                        static_cast<float>(column) *
                            field.denseGridCellSize_,
                    field.denseGridOrigin_.y +
                        static_cast<float>(row) *
                            field.denseGridCellSize_};
                field.expandedBlockerIndex_->queryCandidateIndices(
                    Rect{point, {0.0F, 0.0F}}, blockerCandidates);
                const bool blocked = std::any_of(
                    blockerCandidates.begin(),
                    blockerCandidates.end(),
                    [&](std::size_t index)
                    {
                        return pointInsideOpenRect(
                            point, field.expandedBlockers_[index]);
                    });
                field.denseGridWalkable_[
                    row * field.denseGridColumns_ + column] =
                    blocked ? 0U : 1U;
            }
        }
        field.denseGridConnections_.assign(gridSize, 0U);
        const auto cellPosition = [&](std::size_t row, std::size_t column)
        {
            return Vec2{
                field.denseGridOrigin_.x +
                    static_cast<float>(column) * field.denseGridCellSize_,
                field.denseGridOrigin_.y +
                    static_cast<float>(row) * field.denseGridCellSize_};
        };
        for (std::size_t row{}; row < field.denseGridRows_; ++row)
        {
            for (std::size_t column{};
                 column < field.denseGridColumns_;
                 ++column)
            {
                const std::size_t current =
                    row * field.denseGridColumns_ + column;
                if (field.denseGridWalkable_[current] == 0U)
                {
                    continue;
                }
                for (std::size_t direction{};
                     direction < std::size(kDenseGridNeighborOffsets);
                     ++direction)
                {
                    const int nextColumn = static_cast<int>(column) +
                        kDenseGridNeighborOffsets[direction][0];
                    const int nextRow = static_cast<int>(row) +
                        kDenseGridNeighborOffsets[direction][1];
                    if (nextColumn < 0 || nextRow < 0 ||
                        nextColumn >=
                            static_cast<int>(field.denseGridColumns_) ||
                        nextRow >= static_cast<int>(field.denseGridRows_))
                    {
                        continue;
                    }
                    const std::size_t next =
                        static_cast<std::size_t>(nextRow) *
                            field.denseGridColumns_ +
                        static_cast<std::size_t>(nextColumn);
                    if (field.denseGridWalkable_[next] != 0U &&
                        field.expandedBlockerIndex_->hasLineOfSight(
                            cellPosition(row, column),
                            cellPosition(
                                static_cast<std::size_t>(nextRow),
                                static_cast<std::size_t>(nextColumn))))
                    {
                        field.denseGridConnections_[current] |=
                            static_cast<std::uint8_t>(1U << direction);
                    }
                }
            }
        }
        field.portalChunkSizeCells_ = 16U;
        field.portalChunkColumns_ =
            (field.denseGridColumns_ + field.portalChunkSizeCells_ - 1U) /
            field.portalChunkSizeCells_;
        field.portalChunkRows_ =
            (field.denseGridRows_ + field.portalChunkSizeCells_ - 1U) /
            field.portalChunkSizeCells_;
        field.portalChunkConnections_.assign(
            field.portalChunkColumns_ * field.portalChunkRows_, 0U);
        for (std::size_t row{}; row < field.denseGridRows_; ++row)
        {
            for (std::size_t column{}; column < field.denseGridColumns_;
                 ++column)
            {
                const std::size_t current =
                    row * field.denseGridColumns_ + column;
                const std::size_t chunkRow =
                    row / field.portalChunkSizeCells_;
                const std::size_t chunkColumn =
                    column / field.portalChunkSizeCells_;
                const std::size_t chunk =
                    chunkRow * field.portalChunkColumns_ + chunkColumn;
                for (std::size_t direction{};
                     direction < std::size(kDenseGridNeighborOffsets);
                     ++direction)
                {
                    if ((field.denseGridConnections_[current] &
                         static_cast<std::uint8_t>(1U << direction)) == 0U)
                        continue;
                    const int nextColumn = static_cast<int>(column) +
                        kDenseGridNeighborOffsets[direction][0];
                    const int nextRow = static_cast<int>(row) +
                        kDenseGridNeighborOffsets[direction][1];
                    if (nextColumn < 0 || nextRow < 0)
                        continue;
                    const std::size_t nextChunkColumn =
                        static_cast<std::size_t>(nextColumn) /
                        field.portalChunkSizeCells_;
                    const std::size_t nextChunkRow =
                        static_cast<std::size_t>(nextRow) /
                        field.portalChunkSizeCells_;
                    if (nextChunkColumn != chunkColumn ||
                        nextChunkRow != chunkRow)
                        field.portalChunkConnections_[chunk] |=
                            static_cast<std::uint8_t>(1U << direction);
                }
            }
        }
        return field;
    }

    field.cornerNodes_.reserve(field.expandedBlockers_.size() * 4U);
    for (const Rect &blocker : field.expandedBlockers_)
    {
        const float left = blocker.position.x;
        const float right = blocker.position.x + blocker.size.x;
        const float top = blocker.position.y;
        const float bottom = blocker.position.y + blocker.size.y;
        const Vec2 corners[]{
            Vec2{left, top},
            Vec2{right, top},
            Vec2{right, bottom},
            Vec2{left, bottom}};
        for (const Vec2 corner : corners)
        {
            if (!pointInsideWorldForActor(corner, actorSize, worldSize) ||
                std::any_of(
                    field.expandedBlockers_.begin(),
                    field.expandedBlockers_.end(),
                    [&](const Rect &other)
                    {
                        return pointInsideOpenRect(corner, other);
                    }))
            {
                continue;
            }
            field.cornerNodes_.push_back(corner);
        }
    }

    const std::size_t cornerCount = field.cornerNodes_.size();
    const float infinity = std::numeric_limits<float>::infinity();
    field.cornerEdges_.assign(cornerCount * cornerCount, infinity);
    for (std::size_t first{}; first < cornerCount; ++first)
    {
        field.cornerEdges_[first * cornerCount + first] = 0.0F;
        for (std::size_t second = first + 1U;
             second < cornerCount;
             ++second)
        {
            if (!field.expandedBlockerIndex_->hasLineOfSight(
                    field.cornerNodes_[first],
                    field.cornerNodes_[second]))
            {
                continue;
            }
            const float edgeDistance = distance(
                field.cornerNodes_[first],
                field.cornerNodes_[second]);
            if (!std::isfinite(edgeDistance) ||
                edgeDistance <= kDistanceEpsilon)
            {
                continue;
            }
            field.cornerEdges_[first * cornerCount + second] = edgeDistance;
            field.cornerEdges_[second * cornerCount + first] = edgeDistance;
        }
    }
    return field;
}

std::optional<Vec2> RaidSpaceNavigationField::nextWaypoint(
    Vec2 start,
    Vec2 goal,
    float goalTolerance) const
{
    if (!expandedBlockerIndex_.has_value() ||
        !finite(start) || !finite(goal) ||
        !std::isfinite(goalTolerance) || goalTolerance < 0.0F ||
        !pointInsideWorldForActor(start, actorSize_, worldSize_) ||
        !pointInsideWorldForActor(goal, actorSize_, worldSize_) ||
        std::any_of(
            expandedBlockers_.begin(),
            expandedBlockers_.end(),
            [&](const Rect &blocker)
            {
                return pointInsideOpenRect(start, blocker);
            }))
    {
        return std::nullopt;
    }

    if (distance(start, goal) <= goalTolerance + kDistanceEpsilon)
    {
        return start;
    }

    const bool exactGoalAvailable = std::none_of(
        expandedBlockers_.begin(),
        expandedBlockers_.end(),
        [&](const Rect &blocker)
        {
            return pointInsideOpenRect(goal, blocker);
        });
    if (exactGoalAvailable &&
        expandedBlockerIndex_->hasLineOfSight(start, goal))
    {
        return goal;
    }
    if (usesDenseGrid_)
    {
        return nextDenseGridWaypoint(start, goal, goalTolerance);
    }

    std::vector<Vec2> goalNodes;
    const auto appendGoalNode = [&](Vec2 candidate)
    {
        if (!pointInsideWorldForActor(candidate, actorSize_, worldSize_) ||
            distance(candidate, goal) >
                goalTolerance + kGeometryEpsilon ||
            std::any_of(
                expandedBlockers_.begin(),
                expandedBlockers_.end(),
                [&](const Rect &blocker)
                {
                    return pointInsideOpenRect(candidate, blocker);
                }) ||
            std::any_of(
                goalNodes.begin(),
                goalNodes.end(),
                [&](Vec2 existing)
                {
                    return distance(existing, candidate) <=
                        kDistanceEpsilon;
                }))
        {
            return;
        }
        goalNodes.push_back(candidate);
    };

    appendGoalNode(goal);
    if (goalTolerance > kDistanceEpsilon)
    {
        for (const Rect &blocker : expandedBlockers_)
        {
            if (!pointInsideOpenRect(goal, blocker))
            {
                continue;
            }

            const float left = blocker.position.x;
            const float right = blocker.position.x + blocker.size.x;
            const float top = blocker.position.y;
            const float bottom = blocker.position.y + blocker.size.y;
            const float projectedX = std::clamp(goal.x, left, right);
            const float projectedY = std::clamp(goal.y, top, bottom);
            appendGoalNode(Vec2{left, projectedY});
            appendGoalNode(Vec2{right, projectedY});
            appendGoalNode(Vec2{projectedX, top});
            appendGoalNode(Vec2{projectedX, bottom});
        }
    }

    if (goalNodes.empty())
    {
        return std::nullopt;
    }

    if (goalNodes.front().x == goal.x &&
        goalNodes.front().y == goal.y &&
        expandedBlockerIndex_->hasLineOfSight(start, goal))
    {
        return goal;
    }

    std::vector<Vec2> nodes{start};
    nodes.insert(nodes.end(), goalNodes.begin(), goalNodes.end());
    const std::size_t firstCornerNode = nodes.size();
    nodes.insert(nodes.end(), cornerNodes_.begin(), cornerNodes_.end());

    const std::size_t nodeCount = nodes.size();
    const std::size_t cornerCount = cornerNodes_.size();
    const float infinity = std::numeric_limits<float>::infinity();
    // Static corner-to-corner visibility already lives in cornerEdges_. Only
    // the start/goal rows are dynamic. Keeping those rows separately avoids
    // copying the full O(corners^2) matrix for every enemy refresh.
    std::vector<float> dynamicEdges(
        firstCornerNode * nodeCount,
        infinity);
    for (std::size_t first{}; first < firstCornerNode; ++first)
    {
        dynamicEdges[first * nodeCount + first] = 0.0F;
        for (std::size_t second = first + 1U;
             second < nodeCount;
             ++second)
        {
            if (!expandedBlockerIndex_->hasLineOfSight(
                    nodes[first], nodes[second]))
            {
                continue;
            }
            const float edgeDistance = distance(nodes[first], nodes[second]);
            if (!std::isfinite(edgeDistance) ||
                edgeDistance <= kDistanceEpsilon)
            {
                continue;
            }
            dynamicEdges[first * nodeCount + second] = edgeDistance;
        }
    }
    const auto edgeWeight = [&](std::size_t first, std::size_t second)
    {
        if (first < firstCornerNode)
        {
            return first <= second
                ? dynamicEdges[first * nodeCount + second]
                : dynamicEdges[second * nodeCount + first];
        }
        if (second < firstCornerNode)
        {
            return dynamicEdges[second * nodeCount + first];
        }
        return cornerEdges_[
            (first - firstCornerNode) * cornerCount +
            (second - firstCornerNode)];
    };

    std::vector<float> distances(nodeCount, infinity);
    std::vector<std::size_t> previous(nodeCount, kNoNode);
    std::vector<bool> visited(nodeCount, false);
    distances[0] = 0.0F;

    for (std::size_t iteration{}; iteration < nodeCount; ++iteration)
    {
        std::size_t current = kNoNode;
        for (std::size_t index{}; index < nodeCount; ++index)
        {
            if (visited[index])
            {
                continue;
            }
            if (current == kNoNode ||
                distances[index] < distances[current] - kDistanceEpsilon ||
                (std::abs(distances[index] - distances[current]) <=
                     kDistanceEpsilon &&
                 index < current))
            {
                current = index;
            }
        }
        if (current == kNoNode || !std::isfinite(distances[current]))
        {
            break;
        }
        visited[current] = true;

        for (std::size_t neighbor{}; neighbor < nodeCount; ++neighbor)
        {
            const float edge = edgeWeight(current, neighbor);
            if (visited[neighbor] || !std::isfinite(edge))
            {
                continue;
            }
            const float candidate = distances[current] + edge;
            if (candidate < distances[neighbor] - kDistanceEpsilon ||
                (std::abs(candidate - distances[neighbor]) <=
                     kDistanceEpsilon &&
                 current < previous[neighbor]))
            {
                distances[neighbor] = candidate;
                previous[neighbor] = current;
            }
        }
    }

    std::size_t selectedGoal = kNoNode;
    for (std::size_t goalIndex = 1U;
         goalIndex < firstCornerNode;
         ++goalIndex)
    {
        if (!std::isfinite(distances[goalIndex]) ||
            previous[goalIndex] == kNoNode)
        {
            continue;
        }
        if (selectedGoal == kNoNode ||
            distances[goalIndex] <
                distances[selectedGoal] - kDistanceEpsilon ||
            (std::abs(
                 distances[goalIndex] - distances[selectedGoal]) <=
                 kDistanceEpsilon &&
             goalIndex < selectedGoal))
        {
            selectedGoal = goalIndex;
        }
    }

    if (selectedGoal == kNoNode)
    {
        return std::nullopt;
    }

    std::size_t next = selectedGoal;
    std::size_t guard{};
    while (previous[next] != 0U)
    {
        next = previous[next];
        if (next == kNoNode || ++guard > nodeCount)
        {
            return std::nullopt;
        }
    }
    return nodes[next];
}

Vec2 RaidSpaceNavigationField::actorSize() const noexcept
{
    return actorSize_;
}

Vec2 RaidSpaceNavigationField::worldSize() const noexcept
{
    return worldSize_;
}

std::optional<Vec2> RaidSpaceNavigationField::nextDenseGridWaypoint(
    Vec2 start,
    Vec2 goal,
    float goalTolerance) const
{
    if (!usesDenseGrid_ || denseGridColumns_ == 0U ||
        denseGridRows_ == 0U || denseGridWalkable_.empty() ||
        denseGridConnections_.size() != denseGridWalkable_.size())
    {
        return std::nullopt;
    }

    const auto cellPosition = [&](std::size_t index)
    {
        const std::size_t row = index / denseGridColumns_;
        const std::size_t column = index % denseGridColumns_;
        return Vec2{
            denseGridOrigin_.x +
                static_cast<float>(column) * denseGridCellSize_,
            denseGridOrigin_.y +
                static_cast<float>(row) * denseGridCellSize_};
    };
    const auto nearestWalkable = [&](Vec2 point, float maximumDistance)
        -> std::optional<std::size_t>
    {
        std::optional<std::size_t> selected;
        float selectedDistance = std::numeric_limits<float>::infinity();
        const int centerColumn = std::clamp(
            static_cast<int>(std::lround(
                (point.x - denseGridOrigin_.x) / denseGridCellSize_)),
            0, static_cast<int>(denseGridColumns_ - 1U));
        const int centerRow = std::clamp(
            static_cast<int>(std::lround(
                (point.y - denseGridOrigin_.y) / denseGridCellSize_)),
            0, static_cast<int>(denseGridRows_ - 1U));
        const int radius = static_cast<int>(
            std::ceil(maximumDistance / denseGridCellSize_)) + 1;
        const int firstColumn = std::max(0, centerColumn - radius);
        const int lastColumn = std::min(
            static_cast<int>(denseGridColumns_ - 1U),
            centerColumn + radius);
        const int firstRow = std::max(0, centerRow - radius);
        const int lastRow = std::min(
            static_cast<int>(denseGridRows_ - 1U), centerRow + radius);
        for (int row = firstRow; row <= lastRow; ++row)
        {
            for (int column = firstColumn; column <= lastColumn; ++column)
            {
                const std::size_t index = static_cast<std::size_t>(row) *
                    denseGridColumns_ + static_cast<std::size_t>(column);
                if (denseGridWalkable_[index] == 0U)
                    continue;
                const float candidateDistance =
                    distance(point, cellPosition(index));
                if (candidateDistance > maximumDistance + kGeometryEpsilon ||
                    !expandedBlockerIndex_->hasLineOfSight(
                        point, cellPosition(index)))
                    continue;
                if (!selected.has_value() ||
                    candidateDistance < selectedDistance - kDistanceEpsilon ||
                    (std::abs(candidateDistance - selectedDistance) <=
                         kDistanceEpsilon &&
                     index < *selected))
                {
                    selected = index;
                    selectedDistance = candidateDistance;
                }
            }
        }
        return selected;
    };

    // A final partial grid cell can leave the navigable world edge almost one
    // complete cell away from the last sample. The line-of-sight check above
    // makes this wider snap radius safe while allowing actors near that edge
    // to enter the cached graph.
    const float startSnapDistance = denseGridCellSize_ * 1.5F;
    const float goalSnapDistance = std::max(
        goalTolerance,
        denseGridCellSize_ * 1.5F);
    const std::optional<std::size_t> startCell =
        nearestWalkable(start, startSnapDistance);
    const std::optional<std::size_t> goalCell =
        nearestWalkable(goal, goalSnapDistance);
    if (!startCell.has_value() || !goalCell.has_value())
    {
        return std::nullopt;
    }
    if (*startCell == *goalCell)
    {
        return cellPosition(*startCell);
    }

    std::size_t denseGoalCell = *goalCell;
    const std::size_t startCellRow = *startCell / denseGridColumns_;
    const std::size_t startCellColumn = *startCell % denseGridColumns_;
    const std::size_t goalCellRow = *goalCell / denseGridColumns_;
    const std::size_t goalCellColumn = *goalCell % denseGridColumns_;
    const std::size_t startChunkRow =
        startCellRow / portalChunkSizeCells_;
    const std::size_t startChunkColumn =
        startCellColumn / portalChunkSizeCells_;
    const std::size_t goalChunkRow = goalCellRow / portalChunkSizeCells_;
    const std::size_t goalChunkColumn =
        goalCellColumn / portalChunkSizeCells_;
    const std::size_t startChunk =
        startChunkRow * portalChunkColumns_ + startChunkColumn;
    const std::size_t goalChunk =
        goalChunkRow * portalChunkColumns_ + goalChunkColumn;
    if (startChunk != goalChunk && !portalChunkConnections_.empty())
    {
        std::vector<std::size_t> chunkPrevious(
            portalChunkConnections_.size(), kNoNode);
        std::queue<std::size_t> chunkFrontier;
        chunkPrevious[startChunk] = startChunk;
        chunkFrontier.push(startChunk);
        while (!chunkFrontier.empty() &&
               chunkPrevious[goalChunk] == kNoNode)
        {
            const std::size_t current = chunkFrontier.front();
            chunkFrontier.pop();
            const std::size_t row = current / portalChunkColumns_;
            const std::size_t column = current % portalChunkColumns_;
            for (std::size_t direction{};
                 direction < std::size(kDenseGridNeighborOffsets);
                 ++direction)
            {
                if ((portalChunkConnections_[current] &
                     static_cast<std::uint8_t>(1U << direction)) == 0U)
                    continue;
                const int nextColumn = static_cast<int>(column) +
                    kDenseGridNeighborOffsets[direction][0];
                const int nextRow = static_cast<int>(row) +
                    kDenseGridNeighborOffsets[direction][1];
                if (nextColumn < 0 || nextRow < 0 ||
                    nextColumn >= static_cast<int>(portalChunkColumns_) ||
                    nextRow >= static_cast<int>(portalChunkRows_))
                    continue;
                const std::size_t next =
                    static_cast<std::size_t>(nextRow) * portalChunkColumns_ +
                    static_cast<std::size_t>(nextColumn);
                if (chunkPrevious[next] != kNoNode)
                    continue;
                chunkPrevious[next] = current;
                chunkFrontier.push(next);
            }
        }
        if (chunkPrevious[goalChunk] == kNoNode)
            return std::nullopt;
        std::size_t nextChunk = goalChunk;
        while (chunkPrevious[nextChunk] != startChunk)
        {
            nextChunk = chunkPrevious[nextChunk];
            if (nextChunk == kNoNode)
                return std::nullopt;
        }
        const std::size_t nextChunkRow = nextChunk / portalChunkColumns_;
        const std::size_t nextChunkColumn = nextChunk % portalChunkColumns_;
        std::size_t direction{};
        if (nextChunkRow < startChunkRow) direction = 0U;
        else if (nextChunkColumn < startChunkColumn) direction = 1U;
        else if (nextChunkColumn > startChunkColumn) direction = 2U;
        else direction = 3U;
        const std::size_t firstRow =
            startChunkRow * portalChunkSizeCells_;
        const std::size_t lastRow = std::min(
            denseGridRows_, firstRow + portalChunkSizeCells_);
        const std::size_t firstColumn =
            startChunkColumn * portalChunkSizeCells_;
        const std::size_t lastColumn = std::min(
            denseGridColumns_, firstColumn + portalChunkSizeCells_);
        std::optional<std::size_t> selectedPortal;
        std::size_t selectedDistance = std::numeric_limits<std::size_t>::max();
        for (std::size_t row = firstRow; row < lastRow; ++row)
        {
            for (std::size_t column = firstColumn;
                 column < lastColumn; ++column)
            {
                const std::size_t cell = row * denseGridColumns_ + column;
                if ((denseGridConnections_[cell] &
                     static_cast<std::uint8_t>(1U << direction)) == 0U)
                    continue;
                const int neighborColumn = static_cast<int>(column) +
                    kDenseGridNeighborOffsets[direction][0];
                const int neighborRow = static_cast<int>(row) +
                    kDenseGridNeighborOffsets[direction][1];
                if (neighborColumn < 0 || neighborRow < 0)
                    continue;
                const std::size_t neighbor =
                    static_cast<std::size_t>(neighborRow) * denseGridColumns_ +
                    static_cast<std::size_t>(neighborColumn);
                const std::size_t neighborChunk =
                    (static_cast<std::size_t>(neighborRow) /
                         portalChunkSizeCells_) * portalChunkColumns_ +
                    static_cast<std::size_t>(neighborColumn) /
                        portalChunkSizeCells_;
                if (neighborChunk != nextChunk)
                    continue;
                const std::size_t score =
                    std::max(static_cast<std::size_t>(neighborRow), goalCellRow) -
                        std::min(static_cast<std::size_t>(neighborRow), goalCellRow) +
                    std::max(static_cast<std::size_t>(neighborColumn), goalCellColumn) -
                        std::min(static_cast<std::size_t>(neighborColumn), goalCellColumn);
                if (!selectedPortal.has_value() || score < selectedDistance ||
                    (score == selectedDistance && neighbor < *selectedPortal))
                {
                    selectedPortal = neighbor;
                    selectedDistance = score;
                }
            }
        }
        if (!selectedPortal.has_value())
            return std::nullopt;
        denseGoalCell = *selectedPortal;
    }

    std::vector<std::size_t> previous(
        denseGridWalkable_.size(),
        kNoNode);
    std::vector<float> costs(
        denseGridWalkable_.size(),
        std::numeric_limits<float>::infinity());
    using FrontierEntry = std::pair<float, std::size_t>;
    std::priority_queue<FrontierEntry,
                        std::vector<FrontierEntry>,
                        std::greater<FrontierEntry>> frontier;
    const std::size_t goalRow = denseGoalCell / denseGridColumns_;
    const std::size_t goalColumn = denseGoalCell % denseGridColumns_;
    const auto heuristic = [&](std::size_t index)
    {
        const std::size_t row = index / denseGridColumns_;
        const std::size_t column = index % denseGridColumns_;
        return static_cast<float>(
            std::max(row, goalRow) - std::min(row, goalRow) +
            std::max(column, goalColumn) - std::min(column, goalColumn));
    };
    previous[*startCell] = *startCell;
    costs[*startCell] = 0.0F;
    frontier.push({heuristic(*startCell), *startCell});
    while (!frontier.empty() && previous[denseGoalCell] == kNoNode)
    {
        const std::size_t current = frontier.top().second;
        frontier.pop();
        const std::size_t currentRow = current / denseGridColumns_;
        const std::size_t currentColumn = current % denseGridColumns_;
        for (std::size_t direction{};
             direction < std::size(kDenseGridNeighborOffsets);
             ++direction)
        {
            if ((denseGridConnections_[current] &
                 static_cast<std::uint8_t>(1U << direction)) == 0U)
            {
                continue;
            }
            const int nextColumn =
                static_cast<int>(currentColumn) +
                kDenseGridNeighborOffsets[direction][0];
            const int nextRow = static_cast<int>(currentRow) +
                kDenseGridNeighborOffsets[direction][1];
            if (nextColumn < 0 || nextRow < 0 ||
                nextColumn >= static_cast<int>(denseGridColumns_) ||
                nextRow >= static_cast<int>(denseGridRows_))
            {
                continue;
            }
            const std::size_t next =
                static_cast<std::size_t>(nextRow) * denseGridColumns_ +
                static_cast<std::size_t>(nextColumn);
            if (denseGridWalkable_[next] == 0U)
                continue;
            const float candidate = costs[current] + 1.0F;
            if (candidate + kDistanceEpsilon >= costs[next])
                continue;
            costs[next] = candidate;
            previous[next] = current;
            frontier.push({candidate + heuristic(next), next});
        }
    }
    if (previous[denseGoalCell] == kNoNode)
    {
        return std::nullopt;
    }

    std::size_t next = denseGoalCell;
    std::size_t guard{};
    while (previous[next] != *startCell)
    {
        next = previous[next];
        if (next == kNoNode || ++guard > previous.size())
        {
            return std::nullopt;
        }
    }
    return cellPosition(next);
}

std::optional<Vec2> nextRaidSpaceWaypoint(
    const RaidSpaceNavigationQuery &query)
{
    const std::optional<RaidSpaceNavigationField> field =
        RaidSpaceNavigationField::build(
            query.actorSize,
            query.worldSize,
            query.blockers,
            query.clearance);
    return field.has_value()
        ? field->nextWaypoint(
              query.start,
              query.goal,
              query.goalTolerance)
        : std::nullopt;
}
