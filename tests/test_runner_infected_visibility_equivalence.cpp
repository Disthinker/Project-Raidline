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

// Preserve accepted validity decisions independently of the optimized code.
class OldMainValidityProjection final : public RaidVisibilityProjection
{
public:
    explicit OldMainValidityProjection(const RaidVisibilityProjection &view)
        : RaidVisibilityProjection{view} {}
bool validGeometryReference() const noexcept
{
    if (!valid || geometryRevision == 0 || boundary.size() < 3 ||
        !finite(observer) || !finite(worldSize) || worldSize.x <= 0 ||
        worldSize.y <= 0 || observer.x < 0 || observer.y < 0 ||
        observer.x > worldSize.x || observer.y > worldSize.y ||
        !finite(direction) || !std::isfinite(angleDegrees) ||
        !std::isfinite(maximumDistance) || angleDegrees <= 0 ||
        angleDegrees > 360 || maximumDistance < 0 ||
        (maximumDistance > 0 &&
         (angleDegrees > 180 ||
          std::abs(std::hypot(direction.x, direction.y) - 1) > 0.001F)))
        return false;
    if (!std::isfinite(lightAngleDegrees) || !std::isfinite(lightDistance) ||
        (lightAssetId ? (!lightBatteryId || lightAngleDegrees <= 0 || lightAngleDegrees > 180 || lightDistance <= 0 || lightDistance > 1500 || maximumDistance <= 0) :
                        (lightBatteryId != 0 || lightAngleDegrees != 0 || lightDistance != 0))) return false;
    double area{};
    const float cosLightHalf = std::cos(lightAngleDegrees * 3.14159265358979323846F / 360);
    const float cosHalf =
        std::cos(angleDegrees * 3.14159265358979323846F / 360);
    for (std::size_t i = 0; i < boundary.size(); ++i)
    {
        const auto p = boundary[i], q = boundary[(i + 1) % boundary.size()];
        if (!finite(p) || p.x < 0 || p.y < 0 || p.x > worldSize.x ||
            p.y > worldSize.y)
            return false;
        if (maximumDistance > 0)
        {
            // Far-map float coordinates lose several thousandths on
            // translation. Use 0.01-unit linear roundoff for vertices, rather
            // than a fixed squared tolerance whose effective allowance shrinks
            // at long range.
            const float dx = p.x - observer.x, dy = p.y - observer.y;
            const float squared = dx * dx + dy * dy,
                        dot = dx * direction.x + dy * direction.y;
            const auto within = [&](float distance, float cosine) {
                return squared <= (distance + 0.01F) * (distance + 0.01F) && dot >= -0.01F &&
                       (dot + 0.01F) * (dot + 0.01F) >= squared * cosine * cosine;
            };
            if (!within(maximumDistance, cosHalf) && !(lightAssetId && within(lightDistance, cosLightHalf))) return false;
            if (lightAssetId && angleDegrees != lightAngleDegrees)
            {
                // Each angular subsegment must fit its own range. Testing only
                // vertices (or the enclosing angle/range) fills the union's
                // concave notch, including bridges ending exactly on a seam.
                const Vec2 edge{q.x-p.x, q.y-p.y};
                const float length=std::hypot(edge.x,edge.y);
                const float inner=std::min(angleDegrees,lightAngleDegrees);
                std::array<float,4> cuts{0,1}; std::size_t count=2;
                bool radialStep=false;
                for (float sign : {-1.0F,1.0F})
                {
                    const float theta=inner*sign*3.14159265358979323846F/360;
                    const Vec2 ray{direction.x*std::cos(theta)-direction.y*std::sin(theta),
                                   direction.x*std::sin(theta)+direction.y*std::cos(theta)};
                    const double first=static_cast<double>(dx)*ray.y-static_cast<double>(dy)*ray.x;
                    const double last=static_cast<double>(q.x-observer.x)*ray.y-static_cast<double>(q.y-observer.y)*ray.x;
                    // Float translation at far map coordinates has <=0.01
                    // world-unit roundoff; a radial step has zero ideal area.
                    if (std::abs(first)<=0.01 && std::abs(last)<=0.01) { radialStep=true; break; }
                    const double denominator=first-last;
                    if (std::abs(denominator)<0.000001) continue;
                    const double t=first/denominator;
                    if (t>0 && t<1) cuts[count++]=static_cast<float>(t);
                }
                if (!radialStep)
                {
                    std::sort(cuts.begin(),cuts.begin()+count);
                    for (std::size_t n=1;n<count;++n)
                    {
                        const float a=cuts[n-1], b=cuts[n];
                        if ((b-a)*length<=0.02F) continue;
                        const float mx=dx+edge.x*(a+b)*0.5F, my=dy+edge.y*(a+b)*0.5F;
                        const float radius=std::hypot(mx,my), forward=mx*direction.x+my*direction.y;
                        const float cap=std::max(forward+0.01F>=radius*cosHalf ? maximumDistance : 0.0F,
                                                 forward+0.01F>=radius*cosLightHalf ? lightDistance : 0.0F);
                        for (float t : {a,b})
                            if (std::hypot(dx+edge.x*t,dy+edge.y*t)>cap+0.02F) return false;
                    }
                }
            }
        }
        area += (static_cast<double>(p.x) - observer.x) * (q.y - observer.y) -
                (static_cast<double>(p.y) - observer.y) * (q.x - observer.x);
    }
    return std::isfinite(area) && area > 0.0001;
}
};

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
    EXPECT_EQ(actual.validGeometry(), OldMainValidityProjection{expected}.validGeometryReference());
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

TEST(RunnerInfectedVisibilityEquivalenceTest, PreparedBoundsPreserveOriginalClippingWithDenseCoverAndLightSeams)
{
    const auto index = RaidSpaceBlockerIndex::build({3000, 3000}, denseCover());
    ASSERT_TRUE(index);
    for (const float direction : {0.0F, 60.0F, 120.0F, 180.0F, 240.0F, 300.0F})
        for (const int mode : {0, 1, 2})
        {
            auto key = observation({1216, 800}, heading(direction), mode == 0 ? 180.0F : 100.0F,
                mode == 0 ? 1000.0F : 600.0F);
            if (mode == 2)
            {
                key.lightAssetId = 11;
                key.lightBatteryId = 12;
                key.lightAngleDegrees = 70;
                key.lightDistance = 900;
            }
            const auto view = buildRaidSpaceConeVisibility(*index, key);
            ASSERT_TRUE(OldMainValidityProjection{view}.validGeometryReference());
            ASSERT_TRUE(view.validGeometry());
            const RaidVisibilityBoundsQuery prepared{view};
            for (int target = 0; target < 24; ++target)
            {
                SCOPED_TRACE(::testing::Message() << "direction=" << direction
                    << " mode=" << mode << " target=" << target);
                const auto ray = heading(static_cast<float>(target) * 15);
                const float distance = 50.0F + (target % 7) * 180.0F;
                for (const float size : {0.002F, 50.0F})
                {
                    const Rect bounds{{key.observer.x + ray.x * distance - size / 2,
                        key.observer.y + ray.y * distance - size / 2}, {size, size}};
                    EXPECT_EQ(prepared.boundsVisible(bounds), view.boundsVisible(bounds));
                }
            }
        }
}

TEST(RunnerInfectedVisibilityEquivalenceTest, PreparedBoundsRejectMalformedViewsAndRetainOwnedSnapshot)
{
    const auto index = RaidSpaceBlockerIndex::build({3000, 3000}, {});
    ASSERT_TRUE(index);
    auto view = buildRaidSpaceConeVisibility(*index,
        observation({1000, 1000}, {1, 0}, 110, 500));
    ASSERT_TRUE(OldMainValidityProjection{view}.validGeometryReference());
    const Rect front{{1100, 975}, {50, 50}};
    const RaidVisibilityBoundsQuery owned{view};
    ASSERT_TRUE(owned.boundsVisible(front));
    const auto reject = [&](const RaidVisibilityProjection &bad)
    {
        EXPECT_FALSE(OldMainValidityProjection{bad}.validGeometryReference());
        EXPECT_FALSE(bad.validGeometry());
        EXPECT_FALSE(RaidVisibilityBoundsQuery{bad}.boundsVisible(front));
    };
    auto bad = view;
    bad.valid = false;
    reject(bad);
    bad = view;
    bad.geometryRevision = 0;
    reject(bad);
    bad = view;
    bad.boundary.resize(2);
    reject(bad);
    bad = view;
    bad.boundary.front().x = std::numeric_limits<float>::quiet_NaN();
    reject(bad);
    bad = view;
    bad.boundary.front().y = std::numeric_limits<float>::infinity();
    reject(bad);
    bad = view;
    bad.boundary.front().x = -1;
    reject(bad);
    bad = view;
    bad.boundary.front().x = bad.observer.x + 501;
    reject(bad);
    bad = view;
    std::reverse(bad.boundary.begin(), bad.boundary.end());
    reject(bad);
    bad = view;
    bad.direction = {2, 0};
    reject(bad);
    bad = view;
    bad.angleDegrees = 181;
    reject(bad);
    bad = view;
    bad.maximumDistance = -1;
    reject(bad);
    bad = view;
    bad.maximumDistance = std::numeric_limits<float>::quiet_NaN();
    reject(bad);
    bad = view;
    bad.lightAssetId = 11;
    reject(bad);
    view.boundary.clear();
    EXPECT_TRUE(owned.boundsVisible(front));
    EXPECT_FALSE(RaidVisibilityBoundsQuery{view}.boundsVisible(front));
    EXPECT_FALSE(RaidVisibilityBoundsQuery{}.boundsVisible(front));
    for (const Rect invalid : {Rect{{1100, 975}, {0, 50}},
             Rect{{1100, 975}, {-1, 50}},
             Rect{{1100, 975}, {std::numeric_limits<float>::infinity(), 50}}})
        EXPECT_FALSE(owned.boundsVisible(invalid));
}

TEST(RunnerInfectedVisibilityEquivalenceTest, NearestHitPreservesNearFarAndInterleavedCandidateOrder)
{
    const std::array<BallisticBlocker, 5> geometry{{
        {11, {{1120, 900}, {0.5F, 250}}},
        {12, {{1360, 700}, {2, 600}}},
        {13, {{1720, 500}, {1, 1000}}},
        {14, {{1500, 980}, {20, 40}}},
        // This closer horizontal edge is encountered after an existing hit
        // for some corner rays. Tightening must still admit that nearer hit.
        {15, {{1080, 1010}, {60, 0.002F}}},
    }};
    const std::array<std::array<std::size_t, 5>, 3> orders{{
        {{0, 1, 2, 3, 4}}, {{4, 3, 2, 1, 0}}, {{2, 0, 3, 4, 1}},
    }};
    for (const auto &order : orders)
        for (const float cell : {32.0F, 128.0F, 1024.0F})
        {
            SCOPED_TRACE(::testing::Message() << "first blocker=" << geometry[order[0]].id);
            std::vector<BallisticBlocker> walls;
            for (auto position : order) walls.push_back(geometry[position]);
            const auto index = RaidSpaceBlockerIndex::build({3000, 3000}, walls, cell);
            ASSERT_TRUE(index);
            std::vector<std::size_t> candidates;
            index->queryCandidateIndices({{0, 0}, {3000, 3000}}, candidates);
            ASSERT_EQ(candidates.size(), geometry.size());
            for (std::size_t position = 0; position < candidates.size(); ++position)
                EXPECT_EQ(candidates[position], position);
            EXPECT_FALSE(index->hasLineOfSight({1000, 1000}, {1800, 1000}));
            for (const float degrees : {-12.0F, 0.0F, 12.0F})
                expectIdenticalGeometry({3000, 3000}, walls,
                    observation({1000, 1000}, heading(degrees)), cell);
        }
}

TEST(RunnerInfectedVisibilityEquivalenceTest, TightenedFarCoordinateBoundsKeepCloserGrazingLongThinWalls)
{
    const Vec2 origin{22051, 6031};
    const float infinity = std::numeric_limits<float>::infinity();
    for (const float cell : {32.0F, 128.0F, 1024.0F})
        for (const float width : {0.002F, 0.5F})
            for (const float edge : {std::nextafter(origin.y, -infinity),
                     origin.y, std::nextafter(origin.y, infinity)})
                for (const bool reversed : {false, true})
                {
                    SCOPED_TRACE(::testing::Message() << "edge=" << edge
                        << " width=" << width << " reverse=" << reversed);
                    std::vector<BallisticBlocker> walls{
                        {1, {{22147, 0}, {width, 14400}}},
                        {2, {{22551, 0}, {0.5F, 14400}}},
                        // Its leading corner is closer than either vertical
                        // wall, within one far-coordinate ULP of the axis.
                        {3, {{22083, edge}, {1000, width}}},
                        {4, {{0, 6431}, {25600, width}}},
                    };
                    if (reversed) std::reverse(walls.begin(), walls.end());
                    for (const Vec2 direction : {Vec2{1, 0}, Vec2{1, -0.0F},
                             Vec2{-1, 0}, Vec2{0, 1}, Vec2{0, -1}, heading(45)})
                        expectIdenticalGeometry({25600, 14400}, walls,
                            observation(origin, direction, 180, 1000), cell);
                }
}

TEST(RunnerInfectedVisibilityEquivalenceTest, SignedZeroNearZeroAndRepeatedAnglesKeepEveryBoundaryBit)
{
    const float positiveZero = 0.0F, negativeZero = -0.0F;
    const float nearPositive = std::nextafter(positiveZero, 1.0F);
    const float nearNegative = std::nextafter(positiveZero, -1.0F);
    const std::vector<BallisticBlocker> walls{
        {1, {{1200, 1100}, {20, 10}}},
        {2, {{1200, 1100}, {20, 10}}},
        {3, {{1400, 1200}, {40, 20}}},
        {4, {{1200, 900}, {20, 10}}},
    };
    // Public inputs cover signed/near-zero directions, arcs with a central
    // zero, and repeated collinear corners. They make no private-sort claim.
    for (const float cell : {32.0F, 128.0F, 1024.0F})
        for (const float aperture : {180.0F, 100.0F, 110.0F})
            for (const Vec2 direction : {Vec2{1, positiveZero}, Vec2{1, negativeZero},
                     Vec2{1, nearPositive}, Vec2{1, nearNegative},
                     Vec2{-1, positiveZero}, Vec2{-1, negativeZero},
                     heading(0.000001F), heading(-0.000001F)})
                expectIdenticalGeometry({3000, 3000}, walls,
                    observation({1000, 1000}, direction, aperture, 1000), cell);
}

TEST(RunnerInfectedVisibilityEquivalenceTest, RepeatedNonzeroCornerAnglesAndPoweredSeamsKeepCompleteBoundary)
{
    // The second rectangle is a twofold scaling about the observer, so all
    // four corner angles repeat without requiring a zero-angle corner. The
    // 110-degree enclosing arc has 55 segments and no central arc sample.
    const std::vector<BallisticBlocker> walls{
        {1, {{1200, 1100}, {20, 10}}},
        {2, {{1400, 1200}, {40, 20}}},
        {3, {{1200, 900}, {20, 10}}},
        {4, {{1400, 800}, {40, 20}}},
        {5, {{1200, 1100}, {20, 10}}},
    };
    for (const float cell : {32.0F, 128.0F, 1024.0F})
        for (const auto ranges : {std::array<float, 4>{110, 500, 70, 900},
                 std::array<float, 4>{70, 900, 110, 500}})
            for (const float degrees : {0.0F, 0.00001F, -0.00001F, 179.9999F, -179.9999F})
            {
                auto key = observation({1000, 1000}, heading(degrees), ranges[0], ranges[1]);
                key.lightAssetId = 11;
                key.lightBatteryId = 12;
                key.lightAngleDegrees = ranges[2];
                key.lightDistance = ranges[3];
                // The +/-35-degree range seams remain open, retaining both
                // endpoints and the original corner/unique ordering.
                expectIdenticalGeometry({4000, 3000}, walls, key, cell);
            }
}

TEST(RunnerInfectedVisibilityEquivalenceTest, DenseRepeatedCornersWithZeroAndNearZeroKeepCompleteBoundary)
{
    const std::array<Rect, 4> motifs{{
        // This forward corner is exactly level with the observer. Together
        // with repeated positions it exercises equal and near-zero angles in
        // a large raw angle collection, without assuming a private sort path.
        {{1200, 1000}, {20, 10}},
        {{1200, 1100}, {20, 10}},
        {{1400, 1200}, {40, 20}},
        {{1200, 900}, {20, 10}},
    }};
    std::vector<BallisticBlocker> walls;
    for (std::uint64_t index = 0; index < 72; ++index)
        walls.push_back({index + 1, motifs[index % motifs.size()]});
    ASSERT_EQ(walls.size(), 72U);
    const float nearPositive = std::nextafter(0.0F, 1.0F);
    const float nearNegative = std::nextafter(0.0F, -1.0F);
    for (const float aperture : {110.0F, 180.0F})
        for (const Vec2 direction : {Vec2{1, 0.0F}, Vec2{1, -0.0F},
                 Vec2{1, nearPositive}, Vec2{1, nearNegative},
                 heading(0.000001F), heading(-0.000001F)})
            expectIdenticalGeometry({3000, 3000}, walls,
                observation({1000, 1000}, direction, aperture, 1000), 128);
}

TEST(RunnerInfectedVisibilityEquivalenceTest, EpsilonChainPreservesOriginalOrderedSlabDecisions)
{
    constexpr float epsilon = 0.00001F;
    const std::array<std::array<std::size_t, 4>, 3> orders{{
        {{0, 1, 2, 3}}, {{3, 2, 1, 0}}, {{1, 3, 0, 2}},
    }};
    for (const auto scale : {std::array<float, 2>{1, 0.01F},
             std::array<float, 2>{2, 0.02F}, std::array<float, 2>{4, 0.04F}})
    {
        const Vec2 origin{scale[0], scale[0]};
        const float base = scale[1];
        const std::array<float, 4> fronts{
            base + 2.5F * epsilon, base + 1.8F * epsilon,
            base + 0.9F * epsilon, base};
        for (const auto &order : orders)
            for (const bool farFirst : {false, true})
            {
                SCOPED_TRACE(::testing::Message() << "origin=" << origin.x
                    << " E=" << base << " first chain member=" << order[0]
                    << " far fillers first=" << farFirst);
                std::vector<BallisticBlocker> walls;
                walls.reserve(68);
                const auto appendFar = [&]
                {
                    for (std::uint64_t index = 0; index < 64; ++index)
                        walls.push_back({100 + index,
                            {{origin.x + 1.0F + static_cast<float>(index % 4) * 0.25F,
                                origin.y - 0.2F}, {0.1F, 0.4F}}});
                };
                // Retain far-last and also put fillers first. For F,C,D,seed
                // ordering, the latter exposes seed as the last candidate;
                // a reverse preview can no longer inspect only distant walls.
                if (farFirst) appendFar();
                // F, C, D and seed overlap along the ray. The reference's
                // leave > enter + epsilon dead zone makes intermediate ordered
                // decisions relevant even when a probe found a closer rectangle.
                for (auto member : order)
                    walls.push_back({static_cast<std::uint64_t>(member + 1),
                        {{origin.x + fronts[member], origin.y - 0.2F}, {0.1F, 0.4F}}});
                if (!farFirst) appendFar();
                ASSERT_EQ(walls.size(), 68U);
                for (const float degrees : {0.0F, 0.00001F, -0.00001F})
                    expectIdenticalGeometry({20, 20}, walls,
                        observation(origin, heading(degrees), 180, 10), 2);
            }
    }
}
