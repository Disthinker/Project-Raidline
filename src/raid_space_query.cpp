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

std::optional<Vec2> nextRaidSpaceWaypoint(
    const RaidSpaceNavigationQuery &query)
{
    if (!finite(query.start) || !finite(query.goal) ||
        !finitePositive(query.actorSize) || !finitePositive(query.worldSize) ||
        !std::isfinite(query.clearance) || query.clearance < 0.0F ||
        !pointInsideWorldForActor(
            query.start,
            query.actorSize,
            query.worldSize) ||
        !pointInsideWorldForActor(
            query.goal,
            query.actorSize,
            query.worldSize))
    {
        return std::nullopt;
    }

    if (distance(query.start, query.goal) <= kDistanceEpsilon)
    {
        return query.goal;
    }

    std::vector<Rect> expandedBlockers;
    expandedBlockers.reserve(query.blockers.size());
    for (const BallisticBlocker &blocker : query.blockers)
    {
        if (!finite(blocker.bounds.position) ||
            !finitePositive(blocker.bounds.size))
        {
            return std::nullopt;
        }
        expandedBlockers.push_back(
            expandedForActor(
                blocker.bounds,
                query.actorSize,
                query.clearance));
    }

    if (std::any_of(
            expandedBlockers.begin(),
            expandedBlockers.end(),
            [&](const Rect &blocker)
            {
                return pointInsideOpenRect(query.start, blocker) ||
                       pointInsideOpenRect(query.goal, blocker);
            }))
    {
        return std::nullopt;
    }

    // Most pursuit frames have a direct route. Return before constructing the
    // visibility graph so ordinary visible movement remains proportional to
    // the number of blockers rather than the square of all blocker corners.
    if (segmentClear(query.start, query.goal, expandedBlockers))
    {
        return query.goal;
    }

    std::vector<Vec2> nodes{query.start, query.goal};
    nodes.reserve(2U + expandedBlockers.size() * 4U);
    for (const Rect &blocker : expandedBlockers)
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
            if (!pointInsideWorldForActor(
                    corner,
                    query.actorSize,
                    query.worldSize))
            {
                continue;
            }
            if (std::any_of(
                    expandedBlockers.begin(),
                    expandedBlockers.end(),
                    [&](const Rect &other)
                    {
                        return pointInsideOpenRect(corner, other);
                    }))
            {
                continue;
            }
            nodes.push_back(corner);
        }
    }

    const std::size_t nodeCount = nodes.size();
    const float infinity = std::numeric_limits<float>::infinity();
    std::vector<float> edges(nodeCount * nodeCount, infinity);
    for (std::size_t first{}; first < nodeCount; ++first)
    {
        edges[first * nodeCount + first] = 0.0F;
        for (std::size_t second = first + 1U;
             second < nodeCount;
             ++second)
        {
            if (!segmentClear(
                    nodes[first],
                    nodes[second],
                    expandedBlockers))
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
        if (current == 1U)
        {
            break;
        }

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

    if (!std::isfinite(distances[1U]) || previous[1U] == kNoNode)
    {
        return std::nullopt;
    }

    std::size_t next = 1U;
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
