#include "raid_space_query.h"

#include <gtest/gtest.h>
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

namespace
{
// Frozen test-only oracle from accepted main 77b117d, before the query
// optimization. Keep its original candidate order, angles and slab arithmetic.
// This is a geometry oracle, never another timing gate.
bool finite(Vec2 value) noexcept
{
    return std::isfinite(value.x) && std::isfinite(value.y);
}
bool pointInsideOpenRect(Vec2 point, const Rect &rect) noexcept
{
    constexpr float epsilon = 0.001F;
    return point.x > rect.position.x + epsilon &&
        point.x < rect.position.x + rect.size.x - epsilon &&
        point.y > rect.position.y + epsilon &&
        point.y < rect.position.y + rect.size.y - epsilon;
}

RaidVisibilityProjection
slowConeReference(const RaidSpaceBlockerIndex &index,
                             const RaidObservationKey &key,
                             RaidObservationWork *work)
{
    if (work)
        *work = {};
    RaidVisibilityProjection result;
    result.spaceId = key.spaceId;
    result.observer = key.observer;
    result.worldSize = index.worldSize();
    result.geometryRevision = key.geometryRevision;
    result.direction = key.direction;
    result.angleDegrees = key.angleDegrees;
    result.maximumDistance = key.maximumDistance;
    result.environment = key.environment;
    const auto o = key.observer;
    result.lightAssetId = key.lightAssetId; result.lightBatteryId = key.lightBatteryId;
    result.lightAngleDegrees = key.lightAngleDegrees; result.lightDistance = key.lightDistance;
    const float radius = std::max(key.maximumDistance, key.lightDistance);
    if (!finite(o) || !finite(key.direction) || !std::isfinite(radius) ||
        radius <= 0 || !std::isfinite(key.maximumDistance) || key.maximumDistance <= 0 || !std::isfinite(key.angleDegrees) ||
        key.angleDegrees <= 0 || key.angleDegrees > 180 ||
        std::abs(std::hypot(key.direction.x, key.direction.y) - 1) > 0.001F ||
        o.x < 0 || o.y < 0 || o.x > result.worldSize.x ||
        o.y > result.worldSize.y || key.geometryRevision == 0)
        return result;
    if (!std::isfinite(key.lightDistance) || !std::isfinite(key.lightAngleDegrees) ||
        (key.lightAssetId ? (!key.lightBatteryId || key.lightAngleDegrees <= 0 || key.lightAngleDegrees > 180 || key.lightDistance <= 0 || key.lightDistance > 1500) :
                            (key.lightBatteryId || key.lightAngleDegrees != 0 || key.lightDistance != 0))) return result;
    std::vector<std::size_t> candidates;
    index.queryCandidateIndices(
        {{o.x - radius, o.y - radius}, {radius * 2, radius * 2}}, candidates);
    std::erase_if(candidates,
                  [&](auto i)
                  {
                      const auto r = index.blockerBounds(i);
                      const float x = std::clamp(o.x, r.position.x,
                                                 r.position.x + r.size.x);
                      const float y = std::clamp(o.y, r.position.y,
                                                 r.position.y + r.size.y);
                      return std::hypot(o.x - x, o.y - y) > radius;
                  });
    if (work)
        work->localBlockers = candidates.size();
    for (auto i : candidates)
        if (pointInsideOpenRect(o, index.blockerBounds(i)))
            return result;
    constexpr float pi = 3.14159265358979323846F;
    const float axisAngle = std::atan2(key.direction.y, key.direction.x);
    const float ambientHalf = key.angleDegrees * pi / 360;
    const float lightHalf = key.lightAngleDegrees * pi / 360;
    const float half = std::max(ambientHalf, lightHalf);
    const auto radiusAt = [&](float angle) {
        return std::max(std::abs(angle)<=ambientHalf+0.0000001F ? key.maximumDistance : 0.0F,
                        key.lightAssetId && std::abs(angle)<=lightHalf+0.0000001F ? key.lightDistance : 0.0F);
    };
    std::vector<float> angles;
    // At most one world unit of inward arc error, never revealing beyond range.
    const int arcSegments =
        std::max(2, static_cast<int>(std::ceil(half * 2 / 0.035F)));
    for (int n = 0; n <= arcSegments; ++n)
        angles.push_back(-half + half * 2 * n / arcSegments);
    if (key.lightAssetId)
        for (float edge : {-ambientHalf, ambientHalf, -lightHalf, lightHalf}) angles.push_back(edge);
    const auto corner = [&](Vec2 p)
    {
        const float angle = std::atan2(p.y - o.y, p.x - o.x) - axisAngle;
        const float local = std::atan2(std::sin(angle), std::cos(angle));
        for (float offset : {-0.00001F, 0.0F, 0.00001F})
            if (local + offset >= -half && local + offset <= half)
                angles.push_back(local + offset);
    };
    corner({0, 0});
    corner({result.worldSize.x, 0});
    corner(result.worldSize);
    corner({0, result.worldSize.y});
    for (auto i : candidates)
    {
        const auto r = index.blockerBounds(i);
        corner(r.position);
        corner({r.position.x + r.size.x, r.position.y});
        corner({r.position.x + r.size.x, r.position.y + r.size.y});
        corner({r.position.x, r.position.y + r.size.y});
    }
    if (key.lightAssetId)
        for (auto &angle : angles)
            for (float seam : {ambientHalf,lightHalf})
                if (std::abs(std::abs(angle)-seam)<=0.000002F) angle=std::copysign(seam,angle);
    std::sort(angles.begin(), angles.end());
    angles.erase(std::unique(angles.begin(), angles.end(), [](float a, float b)
                             { return std::abs(a - b) < 1e-7F; }),
                 angles.end());
    result.boundary.reserve(angles.size() + 1);
    for (float angle : angles)
    {
        const Vec2 d{std::cos(axisAngle + angle), std::sin(axisAngle + angle)};
        std::array<float,2> limits{radiusAt(angle),0};
        std::size_t limitCount=1;
        if (key.lightAssetId && (std::abs(std::abs(angle)-ambientHalf)<1e-7F || std::abs(std::abs(angle)-lightHalf)<1e-7F) && std::abs(angle)<half-1e-7F)
        {
            limits={radiusAt(angle-0.000001F),radiusAt(angle+0.000001F)};
            if (limits[0]!=limits[1]) limitCount=2;
        }
        for (std::size_t limitIndex=0; limitIndex<limitCount; ++limitIndex)
        {
        if (work) ++work->rays;
        float length = limits[limitIndex];
        if (d.x > 1e-7F)
            length = std::min(length, (result.worldSize.x - o.x) / d.x);
        else if (d.x < -1e-7F)
            length = std::min(length, -o.x / d.x);
        if (d.y > 1e-7F)
            length = std::min(length, (result.worldSize.y - o.y) / d.y);
        else if (d.y < -1e-7F)
            length = std::min(length, -o.y / d.y);
        for (auto i : candidates)
        {
            if (work)
                ++work->blockerTests;
            const auto r = index.blockerBounds(i);
            float enter = 0, leave = length;
            const auto slab =
                [&](float origin, float movement, float low, float high)
            {
                if (std::abs(movement) < 1e-7F)
                    return origin >= low && origin <= high;
                float a = (low - origin) / movement,
                      b = (high - origin) / movement;
                if (a > b)
                    std::swap(a, b);
                enter = std::max(enter, a);
                leave = std::min(leave, b);
                return enter <= leave;
            };
            if (slab(o.x, d.x, r.position.x, r.position.x + r.size.x) &&
                slab(o.y, d.y, r.position.y, r.position.y + r.size.y) &&
                enter >= 0 && enter <= length && leave > enter + 0.00001F)
                length = enter;
        }
        result.boundary.push_back(
            {std::clamp(o.x + d.x * length, 0.0F, result.worldSize.x),
             std::clamp(o.y + d.y * length, 0.0F, result.worldSize.y)});
        }
    }
    result.boundary.push_back(
        o); // Close both sector edges at its apex, never across the rear.
    result.valid = result.boundary.size() >= 3;
    return result;
}

RaidObservationKey observation(Vec2 observer, Vec2 direction = {1, 0},
    float angle = 180, float distance = 1000)
{
    return {RaidSpaceDefinitionId{"raid_space.runner.cone_equivalence"},
        observer, direction, angle, distance, RaidLightEnvironment::DayOutdoor, 7};
}

void expectIdenticalGeometry(Vec2 worldSize,
    const std::vector<BallisticBlocker> &blockers,
    const RaidObservationKey &key, float cellSize)
{
    SCOPED_TRACE(::testing::Message() << "cell=" << cellSize
        << " observer=" << key.observer.x << ',' << key.observer.y
        << " direction=" << key.direction.x << ',' << key.direction.y
        << " angle=" << key.angleDegrees << " range=" << key.maximumDistance
        << " lamp=" << key.lightAngleDegrees << '/' << key.lightDistance);
    const auto index = RaidSpaceBlockerIndex::build(worldSize, blockers, cellSize);
    ASSERT_TRUE(index);
    RaidObservationWork oldWork, newWork;
    const auto expected = slowConeReference(*index, key, &oldWork);
    const auto actual = buildRaidSpaceConeVisibility(*index, key, &newWork);
    EXPECT_EQ(expected.key(), key);
    EXPECT_EQ(actual.key(), expected.key());
    EXPECT_EQ(actual.valid, expected.valid);
    EXPECT_EQ(actual.validGeometry(), expected.validGeometry());
    EXPECT_EQ(actual.worldSize.x, expected.worldSize.x);
    EXPECT_EQ(actual.worldSize.y, expected.worldSize.y);
    EXPECT_EQ(newWork.rays, oldWork.rays);
    EXPECT_EQ(newWork.localBlockers, oldWork.localBlockers);
    EXPECT_LE(newWork.blockerTests, oldWork.blockerTests);
    ASSERT_EQ(actual.boundary.size(), expected.boundary.size());
    for (std::size_t point{}; point < expected.boundary.size(); ++point)
    {
        SCOPED_TRACE(::testing::Message() << "boundary point=" << point);
        EXPECT_EQ(std::bit_cast<std::uint32_t>(actual.boundary[point].x),
            std::bit_cast<std::uint32_t>(expected.boundary[point].x));
        EXPECT_EQ(std::bit_cast<std::uint32_t>(actual.boundary[point].y),
            std::bit_cast<std::uint32_t>(expected.boundary[point].y));
    }
}

std::vector<BallisticBlocker> denseCover()
{
    // Exactly 220 nearby blockers, including the narrow L-shaped cover from
    // the mixed-actor workload. No blocker contains the observer.
    std::vector<BallisticBlocker> walls{
        {1, {{1320, 680}, {2, 85}}}, {2, {{1320, 765}, {65, 2}}}};
    for (std::uint64_t n{}; walls.size() < 220U; ++n)
        walls.push_back({n + 3U,
            {{866.0F + static_cast<float>(n % 35U) * 20.0F,
                450.0F + static_cast<float>(n / 35U) * 20.0F}, {5, 8}}});
    return walls;
}

Vec2 heading(float degrees)
{
    constexpr float pi = 3.14159265358979323846F;
    const float radians = degrees * pi / 180;
    return {std::cos(radians), std::sin(radians)};
}
} // namespace

TEST(RunnerInfectedVisibilityEquivalenceTest, Dense220KeepsEveryOriginalRayAndBoundaryPoint)
{
    const auto walls = denseCover();
    ASSERT_EQ(walls.size(), 220U);
    for (const float cell : {32.0F, 128.0F, 1024.0F})
        for (const float angle : {0.0F, 45.0F, 90.0F, 135.0F, 180.0F, 270.0F})
        {
            expectIdenticalGeometry({3000, 1800}, walls,
                observation({1216, 800}, heading(angle)), cell);
            expectIdenticalGeometry({3000, 1800}, walls,
                observation({1216, 800}, heading(angle), 100, 600), cell);
        }
}

TEST(RunnerInfectedVisibilityEquivalenceTest, ThinWallsAndNextafterTangenciesMatchTheSlowSlabs)
{
    const float infinity = std::numeric_limits<float>::infinity();
    const float edge = 1000.0F;
    for (const float cell : {32.0F, 128.0F, 1024.0F})
        for (const float width : {0.002F, 0.5F, 5.0F})
            for (const float coordinate : {std::nextafter(edge, -infinity),
                     edge, std::nextafter(edge, infinity)})
            {
                const std::vector<BallisticBlocker> vertical{
                    {1, {{1300, edge}, {width, 300}}},
                    {2, {{1350, 1100}, {2, 10}}},
                    {3, {{1310, 1010}, {3, 3}}}};
                expectIdenticalGeometry({3000, 3000}, vertical,
                    observation({1000, coordinate}), cell);
                const std::vector<BallisticBlocker> horizontal{
                    {1, {{edge, 1300}, {300, width}}},
                    {2, {{1100, 1350}, {10, 2}}}};
                expectIdenticalGeometry({3000, 3000}, horizontal,
                    observation({coordinate, 1000}, {0, 1}), cell);
                const std::vector<BallisticBlocker> corner{
                    {1, {{1300, 1300}, {width, 300}}},
                    {2, {{1300 + width, 1300}, {0.002F, 300}}}};
                expectIdenticalGeometry({3000, 3000}, corner,
                    observation({coordinate, 1000}, heading(45)), cell);
            }
}

TEST(RunnerInfectedVisibilityEquivalenceTest, WrappedAxesFarCoordinatesAndLongWallsMatchExactly)
{
    const std::vector<BallisticBlocker> walls{
        {1, {{21500, 5500}, {0.5F, 2000}}},
        {2, {{21500, 6500}, {600, 0.002F}}},
        {3, {{22500, 0}, {0.5F, 14400}}},
        {4, {{21600, 6000}, {5, 8}}}};
    for (const float cell : {32.0F, 128.0F, 1024.0F})
    {
        for (const Vec2 direction : {Vec2{-1, 0}, Vec2{-1, -0.0F},
                 heading(179.9999F), heading(-179.9999F), Vec2{0, 1}, Vec2{0, -1}})
            expectIdenticalGeometry({25600, 14400}, walls,
                observation({22051, 6031}, direction, 180, 1000), cell);
        // World-edge clipping also keeps rays with a zero initial length.
        expectIdenticalGeometry({25600, 14400}, walls,
            observation({0, 0}, {-1, 0}, 180, 1000), cell);
    }
}

TEST(RunnerInfectedVisibilityEquivalenceTest, LightUnionSeamsKeepBothEndpointsAndOriginalRayOrder)
{
    const std::vector<std::vector<BallisticBlocker>> wallSets{
        {},
        {{1, {{2100, 1700}, {20, 600}}},
         {2, {{2350, 2606.2178F}, {0.002F, 40}}},
         {3, {{1350, 1650}, {0.5F, 700}}}}};
    for (const float cell : {32.0F, 128.0F, 1024.0F})
        for (const auto &walls : wallSets)
            for (const std::array<float, 4> ranges : {
                     std::array<float, 4>{140, 800, 120, 900},
                     std::array<float, 4>{110, 500, 70, 900},
                     std::array<float, 4>{180, 1000, 40, 1500}})
                for (const float direction : {0.0F, 179.9999F, -179.9999F})
                {
                    auto key = observation({2000, 2000}, heading(direction), ranges[0], ranges[1]);
                    key.lightAssetId = 11;
                    key.lightBatteryId = 12;
                    key.lightAngleDegrees = ranges[2];
                    key.lightDistance = ranges[3];
                    expectIdenticalGeometry({4000, 4000}, walls, key, cell);
                }
}
