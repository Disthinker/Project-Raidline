#include "raid_space_query.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <vector>

namespace
{
    constexpr float kGeometryEpsilon{0.001F};
    constexpr float kDistanceEpsilon{0.0001F};
    constexpr std::size_t kNoNode =
        std::numeric_limits<std::size_t>::max();

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

    bool segmentClear(
        Vec2 start,
        Vec2 end,
        const std::vector<Rect> &expandedBlockers) noexcept
    {
        return std::none_of(
            expandedBlockers.begin(),
            expandedBlockers.end(),
            [&](const Rect &blocker)
            {
                return segmentIntersectsRectInterior(start, end, blocker);
            });
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
            return segmentIntersectsRectInterior(
                start,
                end,
                blocker.bounds);
        });
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
            if (!segmentClear(
                    field.cornerNodes_[first],
                    field.cornerNodes_[second],
                    field.expandedBlockers_))
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
    if (!finite(start) || !finite(goal) ||
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
        segmentClear(start, goal, expandedBlockers_))
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
    std::vector<float> edges(nodeCount * nodeCount, infinity);
    for (std::size_t first{}; first < cornerCount; ++first)
    {
        for (std::size_t second{}; second < cornerCount; ++second)
        {
            edges[(firstCornerNode + first) * nodeCount +
                  firstCornerNode + second] =
                cornerEdges_[first * cornerCount + second];
        }
    }
    for (std::size_t first{}; first < firstCornerNode; ++first)
    {
        edges[first * nodeCount + first] = 0.0F;
        for (std::size_t second = first + 1U;
             second < nodeCount;
             ++second)
        {
            if (!segmentClear(nodes[first], nodes[second], expandedBlockers_))
            {
                continue;
            }
            const float edgeDistance = distance(nodes[first], nodes[second]);
            if (!std::isfinite(edgeDistance) ||
                edgeDistance <= kDistanceEpsilon)
            {
                continue;
            }
            edges[first * nodeCount + second] = edgeDistance;
            edges[second * nodeCount + first] = edgeDistance;
        }
    }

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
            const float edge = edges[current * nodeCount + neighbor];
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
