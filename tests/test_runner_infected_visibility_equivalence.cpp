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

// v4 equivalence references frozen from main3ebb63c. These own their data
// and retain the old arithmetic/traversal; no new grouped query is an oracle.
#include <cfenv>
#include <utility>
#if defined(_M_IX86) || defined(_M_X64) || defined(__i386__) || defined(__x86_64__)
#include <xmmintrin.h>
#endif

namespace v4_visibility_reference {
constexpr float kGeometryEpsilon{0.001F};
bool finite(Vec2 p) noexcept
{
    return std::isfinite(p.x) && std::isfinite(p.y);
}
float cross(Vec2 a, Vec2 b, Vec2 p) noexcept
{
    return (b.x - a.x) * (p.y - a.y) - (b.y - a.y) * (p.x - a.x);
}
class FrozenValidityProjection final : public RaidVisibilityProjection {
public:
    explicit FrozenValidityProjection(const RaidVisibilityProjection &v) : RaidVisibilityProjection{v} {}
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
    const float maximumVertexDistanceSquared =
        (maximumDistance + 0.01F) * (maximumDistance + 0.01F);
    const float lightVertexDistanceSquared =
        (lightDistance + 0.01F) * (lightDistance + 0.01F);
    std::array<Vec2, 2> seamRays{};
    if (lightAssetId && angleDegrees != lightAngleDegrees)
    {
        const float inner = std::min(angleDegrees, lightAngleDegrees);
        auto *ray = seamRays.data();
        for (float sign : {-1.0F, 1.0F})
        {
            const float theta = inner * sign * 3.14159265358979323846F / 360;
            *ray++ = {direction.x * std::cos(theta) - direction.y * std::sin(theta),
                      direction.x * std::sin(theta) + direction.y * std::cos(theta)};
        }
    }
    const auto *const points = boundary.data();
    const auto pointCount = boundary.size();
    for (std::size_t i = 0; i < pointCount; ++i)
    {
        const auto p = points[i], q = points[i + 1 == pointCount ? 0 : i + 1];
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
            if (!(squared <= maximumVertexDistanceSquared && dot >= -0.01F &&
                    (dot + 0.01F) * (dot + 0.01F) >= squared * cosHalf * cosHalf) &&
                !(lightAssetId && squared <= lightVertexDistanceSquared && dot >= -0.01F &&
                    (dot + 0.01F) * (dot + 0.01F) >= squared * cosLightHalf * cosLightHalf))
                return false;
            if (lightAssetId && angleDegrees != lightAngleDegrees)
            {
                // Each angular subsegment must fit its own range. Testing only
                // vertices (or the enclosing angle/range) fills the union's
                // concave notch, including bridges ending exactly on a seam.
                const Vec2 edge{q.x-p.x, q.y-p.y};
                const float length=std::hypot(edge.x,edge.y);
                std::array<float,4> cuts{0,1}; std::size_t count=2;
                auto *const cutData = cuts.data();
                bool radialStep=false;
                const auto *const lastRay = seamRays.data() + seamRays.size();
                for (auto rayCursor = seamRays.data(); rayCursor != lastRay; ++rayCursor)
                {
                    const Vec2 ray = *rayCursor;
                    const double first=static_cast<double>(dx)*ray.y-static_cast<double>(dy)*ray.x;
                    const double last=static_cast<double>(q.x-observer.x)*ray.y-static_cast<double>(q.y-observer.y)*ray.x;
                    // Float translation at far map coordinates has <=0.01
                    // world-unit roundoff; a radial step has zero ideal area.
                    if (std::abs(first)<=0.01 && std::abs(last)<=0.01) { radialStep=true; break; }
                    const double denominator=first-last;
                    if (std::abs(denominator)<0.000001) continue;
                    const double t=first/denominator;
                    if (t>0 && t<1) cutData[count++]=static_cast<float>(t);
                }
                if (!radialStep)
                {
                    std::sort(cutData,cutData+count);
                    for (std::size_t n=1;n<count;++n)
                    {
                        const float a=cutData[n-1], b=cutData[n];
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
class FrozenLinearBoundsQuery
{
  public:
    FrozenLinearBoundsQuery() = default;
    explicit FrozenLinearBoundsQuery(const RaidVisibilityProjection &);
    [[nodiscard]] bool boundsVisible(Rect) const noexcept;

  private:
    struct Edge
    {
        Vec2 origin, delta;
        float absoluteX{}, absoluteY{};
        [[nodiscard]] bool overlaps(Vec2 center, float hx, float hy,
                                    float sign) const noexcept;
    };
    struct Triangle
    {
        Vec2 minimum, maximum;
        float sign{};
        std::array<Edge, 3> edges;
    };
    bool valid_{}, bounded_{};
    Vec2 observer_{}, leftDirection_{}, rightDirection_{};
    float maximumDistanceSquared_{};
    std::vector<Triangle> triangles_;
};
bool FrozenLinearBoundsQuery::Edge::overlaps(Vec2 center, float hx, float hy,
                                               float sign) const noexcept
{
    return sign * (delta.x * (center.y - origin.y) -
                   delta.y * (center.x - origin.x)) +
               absoluteX * hy + absoluteY * hx >=
           -0.0001F;
}
FrozenLinearBoundsQuery::FrozenLinearBoundsQuery(
    const RaidVisibilityProjection &view)
{
    if (!FrozenValidityProjection{view}.validGeometryReference())
        return;
    valid_ = true;
    bounded_ = view.maximumDistance > 0;
    observer_ = view.observer;
    const float maximum = std::max(view.maximumDistance, view.lightDistance);
    maximumDistanceSquared_ = maximum * maximum;
    const float half = std::max(view.angleDegrees, view.lightAngleDegrees) * 3.14159265358979323846F / 360;
    const float c = std::cos(half), s = std::sin(half);
    leftDirection_ = {view.direction.x * c + view.direction.y * s,
                      view.direction.y * c - view.direction.x * s};
    rightDirection_ = {view.direction.x * c - view.direction.y * s,
                       view.direction.y * c + view.direction.x * s};
    // Establish live elements before writing through the contiguous view.
    // The final resize removes only triangles rejected by the original area
    // test; no capacity-only writes or changed fan order are permitted.
    triangles_.assign(view.boundary.size(), Triangle{});
    auto *const triangles = triangles_.data();
    std::size_t triangleCount{};
    const auto *const points = view.boundary.data();
    const auto pointCount = view.boundary.size();
    for (std::size_t i = 0; i < pointCount; ++i)
    {
        const auto p = points[i], q = points[i + 1 == pointCount ? 0 : i + 1];
        const auto area = cross(observer_, p, q);
        if (std::abs(area) < 0.00001F)
            continue;
        Vec2 minimum = observer_, maximum = observer_;
        if (p.x < minimum.x) minimum.x = p.x;
        if (q.x < minimum.x) minimum.x = q.x;
        if (p.y < minimum.y) minimum.y = p.y;
        if (q.y < minimum.y) minimum.y = q.y;
        if (maximum.x < p.x) maximum.x = p.x;
        if (maximum.x < q.x) maximum.x = q.x;
        if (maximum.y < p.y) maximum.y = p.y;
        if (maximum.y < q.y) maximum.y = q.y;
        const Vec2 firstDelta{p.x - observer_.x, p.y - observer_.y};
        const Vec2 middleDelta{q.x - p.x, q.y - p.y};
        const Vec2 lastDelta{observer_.x - q.x, observer_.y - q.y};
        triangles[triangleCount++] = {minimum, maximum,
            area > 0 ? 1.0F : -1.0F,
            {Edge{observer_, firstDelta, std::abs(firstDelta.x), std::abs(firstDelta.y)},
             Edge{p, middleDelta, std::abs(middleDelta.x), std::abs(middleDelta.y)},
             Edge{q, lastDelta, std::abs(lastDelta.x), std::abs(lastDelta.y)}}};
    }
    triangles_.resize(triangleCount);
}
bool FrozenLinearBoundsQuery::boundsVisible(Rect rect) const noexcept
{
    if (!valid_ || !finite(rect.position) || !finite(rect.size) ||
        rect.size.x <= 0 || rect.size.y <= 0)
        return false;
    if (bounded_)
    {
        const float x = std::clamp(observer_.x, rect.position.x,
                                   rect.position.x + rect.size.x) -
                        observer_.x;
        const float y = std::clamp(observer_.y, rect.position.y,
                                   rect.position.y + rect.size.y) -
                        observer_.y;
        if (x * x + y * y > maximumDistanceSquared_)
            return false;
        const Vec2 center{rect.position.x + rect.size.x / 2 - observer_.x,
                          rect.position.y + rect.size.y / 2 - observer_.y};
        const auto extent = [&](Vec2 d)
        {
            return std::abs(d.x) * rect.size.y / 2 +
                   std::abs(d.y) * rect.size.x / 2;
        };
        if (leftDirection_.x * center.y - leftDirection_.y * center.x +
                    extent(leftDirection_) <
                -0.0001F ||
            rightDirection_.x * center.y - rightDirection_.y * center.x -
                    extent(rightDirection_) >
                0.0001F)
            return false;
    }
    const float dx = std::min(kRaidSightGeometryEpsilon, rect.size.x * 0.1F);
    const float dy = std::min(kRaidSightGeometryEpsilon, rect.size.y * 0.1F);
    const float left = rect.position.x + dx,
                right = rect.position.x + rect.size.x - dx;
    const float top = rect.position.y + dy,
                bottom = rect.position.y + rect.size.y - dy;
    const Vec2 center{(left + right) / 2, (top + bottom) / 2};
    const float hx = (right - left) / 2, hy = (bottom - top) / 2;
    const auto *const lastTriangle = triangles_.data() + triangles_.size();
    for (auto triangleCursor = triangles_.data(); triangleCursor != lastTriangle; ++triangleCursor)
    {
        const auto &triangle = *triangleCursor;
        if (triangle.maximum.x < left || triangle.minimum.x > right ||
            triangle.maximum.y < top || triangle.minimum.y > bottom)
            continue;
        const auto *const edges = triangle.edges.data();
        if (edges[0].overlaps(center, hx, hy, triangle.sign) &&
            edges[1].overlaps(center, hx, hy, triangle.sign) &&
            edges[2].overlaps(center, hx, hy, triangle.sign))
            return true;
    }
    return false;
}
template <typename Float>
    void sortConeAngles(std::vector<Float> &angles)
    {
        if constexpr (sizeof(Float) != sizeof(std::uint32_t) ||
                      !std::numeric_limits<Float>::is_iec559 ||
                      std::numeric_limits<Float>::radix != 2 ||
                      std::numeric_limits<Float>::digits != 24 ||
                      std::numeric_limits<Float>::max_exponent != 128)
        {
            std::sort(angles.begin(), angles.end());
        }
        else
        {
            auto *const values = angles.data();
            const auto count = angles.size();
            if (count < 256)
            {
                std::sort(values, values + count);
                return;
            }
            std::vector<std::uint32_t> keys(count), scratch(count);
            auto *source = keys.data();
            auto *destination = scratch.data();
            std::array<std::size_t, 4 * 256> histogram{};
            auto *const bins = histogram.data();
            unsigned zeroSigns{};
            for (std::size_t i = 0; i < count; ++i)
            {
                const auto bits = std::bit_cast<std::uint32_t>(values[i]);
                const auto magnitude = bits & 0x7fffffffU;
                const auto exponent = magnitude & 0x7f800000U;
                if (magnitude == 0)
                    zeroSigns |= (bits & 0x80000000U) ? 2U : 1U;
                if (exponent == 0x7f800000U ||
                    (exponent == 0 && magnitude != 0) || zeroSigns == 3)
                {
                    std::sort(values, values + count);
                    return;
                }
                const auto key = bits & 0x80000000U ? ~bits : bits ^ 0x80000000U;
                source[i] = key;
                ++bins[key & 255U];
                ++bins[256 + ((key >> 8) & 255U)];
                ++bins[512 + ((key >> 16) & 255U)];
                ++bins[768 + (key >> 24)];
            }
            // Every buffer already has live elements. Four stable byte
            // passes produce the same bit sequence as float < for eligible
            // values, including a group containing only one sign of zero.
            for (unsigned pass = 0; pass < 4; ++pass)
            {
                auto *const offsets = bins + pass * 256;
                std::size_t sum{};
                for (std::size_t bin = 0; bin < 256; ++bin)
                {
                    const auto size = offsets[bin];
                    offsets[bin] = sum;
                    sum += size;
                }
                const auto shift = pass * 8;
                for (std::size_t i = 0; i < count; ++i)
                {
                    const auto key = source[i];
                    destination[offsets[(key >> shift) & 255U]++] = key;
                }
                std::swap(source, destination);
            }
            for (std::size_t i = 0; i < count; ++i)
            {
                const auto key = source[i];
                const auto bits = key & 0x80000000U ? key ^ 0x80000000U : ~key;
                values[i] = std::bit_cast<Float>(bits);
            }
        }
    }
bool castConeRay(const Rect &rect, const Vec2 &origin, const Vec2 &direction,
                     float absoluteX, float absoluteY, float maximum,
                     float &hitDistance) noexcept
    {
        float enter = 0, leave = maximum;
        if (absoluteX < 1e-7F)
        {
            if (!(origin.x >= rect.position.x && origin.x <= rect.position.x + rect.size.x))
                return false;
        }
        else
        {
            float a = (rect.position.x - origin.x) / direction.x,
                  b = (rect.position.x + rect.size.x - origin.x) / direction.x;
            if (a > b) { const float temporary = a; a = b; b = temporary; }
            if (enter < a) enter = a;
            if (b < leave) leave = b;
            if (!(enter <= leave)) return false;
        }
        if (absoluteY < 1e-7F)
        {
            if (!(origin.y >= rect.position.y && origin.y <= rect.position.y + rect.size.y))
                return false;
        }
        else
        {
            float a = (rect.position.y - origin.y) / direction.y,
                  b = (rect.position.y + rect.size.y - origin.y) / direction.y;
            if (a > b) { const float temporary = a; a = b; b = temporary; }
            if (enter < a) enter = a;
            if (b < leave) leave = b;
            if (!(enter <= leave)) return false;
        }
        if (!(enter >= 0 && enter <= maximum && leave > enter + 0.00001F))
            return false;
        hitDistance = enter;
        return true;
    }
bool pointInsideOpenRect(Vec2 point, const Rect &rect) noexcept
    {
        return point.x > rect.position.x + kGeometryEpsilon &&
               point.x < rect.position.x + rect.size.x - kGeometryEpsilon &&
               point.y > rect.position.y + kGeometryEpsilon &&
               point.y < rect.position.y + rect.size.y - kGeometryEpsilon;
    }
// Own a separate original cell index so the cone oracle cannot accidentally
// inherit a bug in the new direct-merge query. Build/query expressions and
// original-index order are frozen; only old input data is borrowed to copy.
class FrozenCandidateIndex {
    Vec2 world_{}, half_{};
    float cell_{};
    std::size_t columns_{}, rows_{};
    std::vector<Rect> bounds_;
    std::vector<std::vector<std::size_t>> cells_;
    std::size_t column(float x) const {
        return static_cast<std::size_t>(std::clamp(std::floor(x / cell_), 0.0F,
            static_cast<float>(columns_ - 1U)));
    }
    std::size_t row(float y) const {
        return static_cast<std::size_t>(std::clamp(std::floor(y / cell_), 0.0F,
            static_cast<float>(rows_ - 1U)));
    }
public:
    explicit FrozenCandidateIndex(const RaidSpaceBlockerIndex &source)
        : world_(source.worldSize()), cell_(source.cellSize()) {
        columns_ = static_cast<std::size_t>(std::max(1.0F, std::ceil(world_.x / cell_)));
        rows_ = static_cast<std::size_t>(std::max(1.0F, std::ceil(world_.y / cell_)));
        cells_.resize(columns_ * rows_);
        const auto bounds = source.allBlockerBounds();
        bounds_.assign(bounds.begin(), bounds.end());
        for (std::size_t n = 0; n < bounds_.size(); ++n) {
            const auto rect = bounds_[n];
            half_.x = std::max(half_.x, rect.size.x * 0.5F);
            half_.y = std::max(half_.y, rect.size.y * 0.5F);
            const Vec2 center{rect.position.x + rect.size.x * 0.5F,
                              rect.position.y + rect.size.y * 0.5F};
            cells_[row(center.y) * columns_ + column(center.x)].push_back(n);
        }
    }
    Vec2 worldSize() const { return world_; }
    std::span<const Rect> allBlockerBounds() const { return bounds_; }
    void queryCandidateIndices(Rect bounds, std::vector<std::size_t> &output) const {
        output.clear();
        if (!finite(bounds.position) || !finite(bounds.size) ||
            bounds.size.x < 0 || bounds.size.y < 0 || cells_.empty()) return;
        const auto firstColumn = column(bounds.position.x - half_.x);
        const auto lastColumn = column(bounds.position.x + bounds.size.x + half_.x);
        const auto firstRow = row(bounds.position.y - half_.y);
        const auto lastRow = row(bounds.position.y + bounds.size.y + half_.y);
        for (auto y = firstRow; y <= lastRow; ++y)
            for (auto x = firstColumn; x <= lastColumn; ++x) {
                const auto &cell = cells_[y * columns_ + x];
                output.insert(output.end(), cell.begin(), cell.end());
            }
        std::sort(output.begin(), output.end());
    }
};

RaidVisibilityProjection
frozenPreV4Cone(const FrozenCandidateIndex &index,
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
    const auto *const blockerData = index.allBlockerBounds().data();
    std::erase_if(candidates,
                  [&](auto i)
                  {
                      const auto r = blockerData[i];
                      const float x = std::clamp(o.x, r.position.x,
                                                 r.position.x + r.size.x);
                      const float y = std::clamp(o.y, r.position.y,
                                                 r.position.y + r.size.y);
                      return std::hypot(o.x - x, o.y - y) > radius;
                  });
    if (work)
        work->localBlockers = candidates.size();
    for (auto i : candidates)
        if (pointInsideOpenRect(o, blockerData[i]))
            return result;
    constexpr float pi = 3.14159265358979323846F;
    const float axisAngle = std::atan2(key.direction.y, key.direction.x);
    const float ambientHalf = key.angleDegrees * pi / 360;
    const float lightHalf = key.lightAngleDegrees * pi / 360;
    const float half = std::max(ambientHalf, lightHalf);
    const auto radiusAt = [&](float angle) {
        const float ambient = std::abs(angle) <= ambientHalf + 0.0000001F ? key.maximumDistance : 0.0F;
        const float lamp = key.lightAssetId && std::abs(angle) <= lightHalf + 0.0000001F ? key.lightDistance : 0.0F;
        return ambient < lamp ? lamp : ambient;
    };
    // At most one world unit of inward arc error, never revealing beyond range.
    const int arcSegments =
        std::max(2, static_cast<int>(std::ceil(half * 2 / 0.035F)));
    // One arc, four optional light seams, and three rays per world/blocker
    // corner. Establish valid size before borrowing writable storage.
    std::vector<float> angles(static_cast<std::size_t>(arcSegments) + 1 +
        (key.lightAssetId ? 4 : 0) + 12 * (candidates.size() + 1));
    auto *angleWrite = angles.data();
    for (int n = 0; n <= arcSegments; ++n)
        *angleWrite++ = -half + half * 2 * n / arcSegments;
    if (key.lightAssetId)
        for (float edge : {-ambientHalf, ambientHalf, -lightHalf, lightHalf}) *angleWrite++ = edge;
    const auto corner = [&](Vec2 p)
    {
        const float angle = std::atan2(p.y - o.y, p.x - o.x) - axisAngle;
        const float local = std::atan2(std::sin(angle), std::cos(angle));
        for (float offset : {-0.00001F, 0.0F, 0.00001F})
            if (local + offset >= -half && local + offset <= half)
                *angleWrite++ = local + offset;
    };
    corner({0, 0});
    corner({result.worldSize.x, 0});
    corner(result.worldSize);
    corner({0, result.worldSize.y});
    for (auto i : candidates)
    {
        const auto r = blockerData[i];
        corner(r.position);
        corner({r.position.x + r.size.x, r.position.y});
        corner({r.position.x + r.size.x, r.position.y + r.size.y});
        corner({r.position.x, r.position.y + r.size.y});
    }
    angles.resize(static_cast<std::size_t>(angleWrite - angles.data()));
    if (key.lightAssetId)
        for (auto &angle : angles)
            for (float seam : {ambientHalf,lightHalf})
                if (std::abs(std::abs(angle)-seam)<=0.000002F) angle=std::copysign(seam,angle);
    sortConeAngles(angles);
    auto *const angleData = angles.data();
    const auto *const uniqueEnd = std::unique(angleData, angleData + angles.size(),
        [](float a, float b) { return std::abs(a - b) < 1e-7F; });
    angles.resize(static_cast<std::size_t>(uniqueEnd - angleData));
    // Only a powered light union can emit two endpoints for one angle.
    // Copy one initialized value to establish live points before bulk writes.
    result.boundary.assign((key.lightAssetId ? 2 : 1) * angles.size() + 1, Vec2{});
    auto *boundaryWrite = result.boundary.data();
    // Keep the original corner rays and original-index slab ordering. A ray
    // can only hit local blockers overlapping its closed segment bounds.
    // Borrow the immutable bounds once and use a closed broad-phase test
    // before the original narrow phase, without per-ray allocation or sorting.
    struct ConeBlocker
    {
        Rect bounds;
        Vec2 relativeCenter, halfExtent;
        float linePadding;
    };
    std::vector<ConeBlocker> localBounds;
    localBounds.reserve(candidates.size());
    for (auto i : candidates)
    {
        const auto r = blockerData[i];
        const Vec2 halfExtent{r.size.x * 0.5F, r.size.y * 0.5F};
        const Vec2 relativeCenter{r.position.x + halfExtent.x - o.x,
                                  r.position.y + halfExtent.y - o.y};
        // Conservative roundoff allowance for a line touching a corner. This
        // only admits extra narrow-phase candidates, never grows visibility.
        const float padding = 8 * std::numeric_limits<float>::epsilon() *
            (std::abs(o.x) + std::abs(o.y) + std::abs(relativeCenter.x) +
             std::abs(relativeCenter.y) + halfExtent.x + halfExtent.y + 1);
        localBounds.push_back({r, relativeCenter, halfExtent, padding});
    }
    // Conservative angular buckets avoid scanning every local rectangle for
    // every ray. Each bounding circle covers its whole rectangle; padded bin
    // ranges may admit extras. Insertion keeps the original blocker order.
    constexpr std::size_t bucketCount = 512;
    struct BucketRange { std::size_t candidate, first, last; };
    std::vector<BucketRange> bucketRanges;
    bucketRanges.reserve(localBounds.size() * 2);
    std::array<std::size_t, bucketCount> bucketSizes{};
    auto *const bucketSizeData = bucketSizes.data();
    const auto addRange = [&](std::size_t candidate, std::size_t first, std::size_t last)
    {
        bucketRanges.push_back({candidate, first, last});
        for (std::size_t bin = first; bin <= last; ++bin) ++bucketSizeData[bin];
    };
    const auto bucketFor = [&](float angle)
    {
        if (half <= 0) return std::size_t{0};
        const float raw = (angle + half) * static_cast<float>(bucketCount) / (half * 2);
        const float last = static_cast<float>(bucketCount - 1);
        return static_cast<std::size_t>(raw < 0.0F ? 0.0F : last < raw ? last : raw);
    };
    const auto *const localData = localBounds.data();
    for (std::size_t i = 0; i < localBounds.size(); ++i)
    {
        const auto &b = localData[i];
        const float centerDistance = std::hypot(b.relativeCenter.x, b.relativeCenter.y);
        const float circleRadius = std::hypot(b.halfExtent.x, b.halfExtent.y) + b.linePadding;
        if (half <= 0 || !std::isfinite(circleRadius) || circleRadius >= centerDistance)
        {
            addRange(i, 0, bucketCount - 1);
            continue;
        }
        const float delta = std::atan2(b.relativeCenter.y, b.relativeCenter.x) - axisAngle;
        const float centerAngle = std::atan2(std::sin(delta), std::cos(delta));
        const float angularRadius = std::asin(std::clamp(circleRadius / centerDistance, 0.0F, 1.0F));
        for (float wrap : {-2 * pi, 0.0F, 2 * pi})
        {
            const float low = centerAngle + wrap - angularRadius;
            const float high = centerAngle + wrap + angularRadius;
            if (low > half || high < -half) continue;
            const auto first = bucketFor(std::max(low, -half));
            const auto last = bucketFor(std::min(high, half));
            const auto begin = first > 0 ? first - 1 : 0;
            const auto end = std::min(last + 1, bucketCount - 1);
            addRange(i, begin, end);
        }
    }
    std::array<std::size_t, bucketCount + 1> bucketOffsets{};
    auto *const bucketOffsetData = bucketOffsets.data();
    for (std::size_t bin = 0; bin < bucketCount; ++bin)
        bucketOffsetData[bin + 1] = bucketOffsetData[bin] + bucketSizeData[bin];
    std::vector<std::size_t> angularCandidates(bucketOffsets.back());
    auto writeOffsets = bucketOffsets;
    auto *const writeOffsetData = writeOffsets.data();
    auto *const angularData = angularCandidates.data();
    for (const auto range : bucketRanges)
        for (std::size_t bin = range.first; bin <= range.last; ++bin)
            angularData[writeOffsetData[bin]++] = range.candidate;
    // A preview only bounds the broad phase, never the ordered ray result.
    // The original epsilon can transmit a different accept/reject decision
    // through a chain of near-equal hits. Budget one worst-case addition gap
    // per candidate, using the whole radius as a uniform float-ULP bound.
    // The extra ULP and outward rounding retain uncertain boundary cases.
    const float hitEpsilon = 0.00001F;
    const float additionCap = radius + hitEpsilon;
    const float nextAddition = std::nextafter(additionCap, std::numeric_limits<float>::infinity());
    const bool previewBudgetValid = std::isfinite(additionCap) && std::isfinite(nextAddition);
    const double candidateSlack = previewBudgetValid ? static_cast<double>(hitEpsilon) +
        2 * (static_cast<double>(nextAddition) - additionCap) : 0.0;
    const auto *const lastAngle = angles.data() + angles.size();
    for (auto angleCursor = angles.data(); angleCursor != lastAngle; ++angleCursor)
    {
        const float angle = *angleCursor;
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
        {
            const float cap = (result.worldSize.x - o.x) / d.x;
            if (cap < length) length = cap;
        }
        else if (d.x < -1e-7F)
        {
            const float cap = -o.x / d.x;
            if (cap < length) length = cap;
        }
        if (d.y > 1e-7F)
        {
            const float cap = (result.worldSize.y - o.y) / d.y;
            if (cap < length) length = cap;
        }
        else if (d.y < -1e-7F)
        {
            const float cap = -o.y / d.y;
            if (cap < length) length = cap;
        }
        const Vec2 end{o.x + d.x * length, o.y + d.y * length};
        float left = end.x < o.x ? end.x : o.x, right = o.x < end.x ? end.x : o.x;
        float top = end.y < o.y ? end.y : o.y, bottom = o.y < end.y ? end.y : o.y;
        const float absoluteX = std::abs(d.x), absoluteY = std::abs(d.y);
        const auto bin = bucketFor(angle);
        const auto candidateCount = bucketOffsetData[bin + 1] - bucketOffsetData[bin];
        const auto *const firstCandidate = candidateCount ? angularData + bucketOffsetData[bin] : nullptr;
        auto previewCursor = candidateCount ? firstCandidate + candidateCount : nullptr;
        bool previewFound = false;
        // A reverse coarse scan finds one useful actual slab to preview.
        // Later entries proven outside the original segment need no second
        // scan. All surviving decisions are consumed in original order.
        while (previewCursor != firstCandidate)
        {
            --previewCursor;
            const auto &candidate = localData[*previewCursor];
            const auto &r = candidate.bounds;
            const float pad = candidate.linePadding;
            if (r.position.x + r.size.x < left - pad || r.position.x > right + pad ||
                r.position.y + r.size.y < top - pad || r.position.y > bottom + pad) continue;
            const float cross = d.x * candidate.relativeCenter.y - d.y * candidate.relativeCenter.x;
            const float extent = absoluteX * candidate.halfExtent.y + absoluteY * candidate.halfExtent.x;
            if (std::abs(cross) > extent + pad) continue;
            previewFound = true;
            break;
        }
        const auto *const lastCandidate = previewFound ? previewCursor + 1 : firstCandidate;
        float previewEntry{}, previewThreshold{}, coarseLength = length;
        bool previewHit = false;
        if (previewFound)
        {
            if (work) ++work->blockerTests;
            previewHit = castConeRay(localData[*previewCursor].bounds, o, d,
                                     absoluteX, absoluteY, length, previewEntry);
            if (previewHit)
            {
                previewThreshold = previewEntry + hitEpsilon;
                // A successful preview has finite 0 <= entry < threshold
                // (or threshold==entry after rounding) < leave <= length.
                // The positive bounded budget cannot produce NaN; an upper
                // bound outside this segment simply leaves the old scan.
                if (previewBudgetValid)
                {
                    const double limit = static_cast<double>(previewThreshold) +
                        static_cast<double>(candidateCount) * candidateSlack;
                    if (limit < static_cast<double>(length))
                    {
                        const float cap = std::nextafter(static_cast<float>(limit),
                                                        std::numeric_limits<float>::infinity());
                        if (cap < length)
                        {
                            coarseLength = cap;
                            const Vec2 closestEnd{o.x + d.x * coarseLength, o.y + d.y * coarseLength};
                            left = closestEnd.x < o.x ? closestEnd.x : o.x;
                            right = o.x < closestEnd.x ? closestEnd.x : o.x;
                            top = closestEnd.y < o.y ? closestEnd.y : o.y;
                            bottom = o.y < closestEnd.y ? closestEnd.y : o.y;
                        }
                    }
                }
            }
        }
        for (auto cursor = firstCandidate; cursor != lastCandidate; ++cursor)
        {
            if (cursor == previewCursor)
            {
                // Preview leave was min(L0, both axis exits) and exceeded
                // T=entry+epsilon. With L<=L0 the original acceptance is
                // exactly L>T. A miss at L0 cannot become a hit at smaller L.
                // This is current-ray reuse only; count the actual slab once.
                if (previewHit && length > previewThreshold) length = previewEntry;
                continue;
            }
            const auto &candidate = localData[*cursor];
            const auto &r = candidate.bounds;
            const float pad = candidate.linePadding;
            if (r.position.x + r.size.x < left - pad || r.position.x > right + pad ||
                r.position.y + r.size.y < top - pad || r.position.y > bottom + pad) continue;
            const float cross = d.x * candidate.relativeCenter.y -
                                d.y * candidate.relativeCenter.x;
            const float extent = absoluteX * candidate.halfExtent.y +
                                 absoluteY * candidate.halfExtent.x;
            if (std::abs(cross) > extent + pad) continue;
            if (work)
                ++work->blockerTests;
            float enter{};
            if (castConeRay(r, o, d, absoluteX, absoluteY, length, enter))
            {
                length = enter;
                // A nearer hit shortens the remaining closed segment. Keep
                // the original per-blocker padding and candidate order; this
                // only skips later slabs beyond the current closest hit.
                if (length < coarseLength) coarseLength = length;
                const Vec2 closestEnd{o.x + d.x * coarseLength, o.y + d.y * coarseLength};
                left = closestEnd.x < o.x ? closestEnd.x : o.x;
                right = o.x < closestEnd.x ? closestEnd.x : o.x;
                top = closestEnd.y < o.y ? closestEnd.y : o.y;
                bottom = o.y < closestEnd.y ? closestEnd.y : o.y;
            }
        }
        const float x = o.x + d.x * length, y = o.y + d.y * length;
        *boundaryWrite++ =
            {x < 0.0F ? 0.0F : result.worldSize.x < x ? result.worldSize.x : x,
             y < 0.0F ? 0.0F : result.worldSize.y < y ? result.worldSize.y : y};
        }
    }
    *boundaryWrite++ = o; // Close both sector edges at the original apex.
    result.boundary.resize(static_cast<std::size_t>(boundaryWrite - result.boundary.data()));
    result.valid = result.boundary.size() >= 3;
    return result;
}
} // namespace v4_visibility_reference
namespace v4_visibility_reference {

struct RestoreEnvironment {
    std::fenv_t environment{};
#if defined(_M_IX86) || defined(_M_X64) || defined(__i386__) || defined(__x86_64__)
    unsigned mxcsr = _mm_getcsr();
#endif
    RestoreEnvironment() { std::fegetenv(&environment); }
    ~RestoreEnvironment() {
        std::fesetenv(&environment);
#if defined(_M_IX86) || defined(_M_X64) || defined(__i386__) || defined(__x86_64__)
        _mm_setcsr(mxcsr);
#endif
    }
};

RaidVisibilityProjection regularFan(std::size_t triangles, Vec2 center = {512, 512},
                                   float radius = 240, Vec2 world = {1024, 1024}) {
    RaidVisibilityProjection view;
    view.valid = true;
    view.geometryRevision = 17;
    view.spaceId = RaidSpaceDefinitionId{"raid_space.v4.frozen_bounds"};
    view.observer = center;
    view.worldSize = world;
    view.direction = {1, 0};
    view.angleDegrees = 360;
    view.maximumDistance = 0;
    for (std::size_t n = 0; n < triangles; ++n) {
        const float angle = static_cast<float>(n) * 6.2831853071795864769F /
                            static_cast<float>(triangles);
        view.boundary.push_back({center.x + std::cos(angle) * radius,
                                 center.y + std::sin(angle) * radius});
    }
    return view;
}

std::vector<Rect> probes(const RaidVisibilityProjection &view) {
    const auto o = view.observer;
    const float infinity = std::numeric_limits<float>::infinity();
    std::vector<Rect> result{
        {{o.x - 1, o.y - 1}, {2, 2}}, {{o.x, o.y}, {0.002F, 0.002F}},
        {{o.x - 0.0001F, o.y - 0.0001F}, {0.0002F, 0.0002F}},
        {{o.x - 400, o.y - 20}, {800, 40}}, {{-100, -100}, {1, 1}},
        {{o.x + 300, o.y + 300}, {2, 2}}, {{-0.0F, 0.0F}, {0.002F, 0.002F}}};
    for (std::size_t n : {std::size_t{0}, view.boundary.size() / 4,
             view.boundary.size() / 2, view.boundary.size() - 1}) {
        const auto point = view.boundary[n];
        for (float x : {std::nextafter(point.x, -infinity), point.x,
                 std::nextafter(point.x, infinity)})
            for (float width : {0.00001F, 0.002F, 0.1F, 3.0F})
                result.push_back({{x, point.y - width * 0.5F}, {width, width}});
    }
    const float maximum = std::numeric_limits<float>::max();
    result.push_back({{maximum * 0.75F, maximum * 0.75F}, {maximum, maximum}});
    result.push_back({{-maximum, -maximum}, {maximum, maximum}});
    return result;
}

void compareBounds(const RaidVisibilityBoundsQuery &actual,
                   const FrozenLinearBoundsQuery &expected,
                   const std::vector<Rect> &queries) {
    for (std::size_t n = 0; n < queries.size(); ++n) {
        SCOPED_TRACE(testing::Message() << "query=" << n);
        EXPECT_EQ(actual.boundsVisible(queries[n]), expected.boundsVisible(queries[n]));
    }
}

void sameBoundary(const RaidVisibilityProjection &actual,
                  const RaidVisibilityProjection &expected) {
    EXPECT_EQ(actual.key(), expected.key());
    EXPECT_EQ(actual.valid, expected.valid);
    EXPECT_EQ(std::bit_cast<std::uint32_t>(actual.worldSize.x),
              std::bit_cast<std::uint32_t>(expected.worldSize.x));
    EXPECT_EQ(std::bit_cast<std::uint32_t>(actual.worldSize.y),
              std::bit_cast<std::uint32_t>(expected.worldSize.y));
    ASSERT_EQ(actual.boundary.size(), expected.boundary.size());
    for (std::size_t n = 0; n < actual.boundary.size(); ++n) {
        SCOPED_TRACE(testing::Message() << "point=" << n);
        EXPECT_EQ(std::bit_cast<std::uint32_t>(actual.boundary[n].x),
                  std::bit_cast<std::uint32_t>(expected.boundary[n].x));
        EXPECT_EQ(std::bit_cast<std::uint32_t>(actual.boundary[n].y),
                  std::bit_cast<std::uint32_t>(expected.boundary[n].y));
    }
}

void compareCone(const RaidSpaceBlockerIndex &index, const RaidObservationKey &key,
                 const FrozenCandidateIndex *frozenIndex = nullptr) {
    std::optional<FrozenCandidateIndex> localReference;
    if (!frozenIndex) { localReference.emplace(index); frozenIndex = &*localReference; }
    RaidObservationWork previousWork{}, slowWork{}, actualWork{};
    const auto previous = frozenPreV4Cone(*frozenIndex, key, &previousWork);
    const auto slow = slowConeReference(index, key, &slowWork);
    const auto actual = buildRaidSpaceConeVisibility(index, key, &actualWork);
    sameBoundary(actual, previous);
    sameBoundary(actual, slow);
    EXPECT_EQ(actualWork.localBlockers, previousWork.localBlockers);
    EXPECT_EQ(actualWork.rays, previousWork.rays);
    EXPECT_EQ(actualWork.blockerTests, previousWork.blockerTests);
    EXPECT_EQ(actualWork.localBlockers, slowWork.localBlockers);
    EXPECT_EQ(actualWork.rays, slowWork.rays);
    // The slow all-candidate oracle intentionally executes extra slab tests.
    EXPECT_LE(actualWork.blockerTests, slowWork.blockerTests);
    EXPECT_EQ(actual.validGeometry(), FrozenValidityProjection{slow}.validGeometryReference());
}
} // namespace v4_visibility_reference

TEST(RunnerInfectedVisibilityEquivalenceTest, GroupedBoundsMatchFrozenLinearAtThresholdTailAndApex) {
    using namespace v4_visibility_reference;
    for (std::size_t count : {3U, 31U, 32U, 33U, 127U, 128U, 129U, 159U, 160U, 161U}) {
        SCOPED_TRACE(testing::Message() << "retained triangles=" << count);
        const auto view = regularFan(count);
        ASSERT_TRUE(FrozenValidityProjection{view}.validGeometryReference());
        compareBounds(RaidVisibilityBoundsQuery{view}, FrozenLinearBoundsQuery{view}, probes(view));
    }
    // These zero-area duplicate edges do not contribute retained triangles.
    auto view = regularFan(129);
    view.boundary.insert(view.boundary.begin() + 32, view.boundary[32]);
    view.boundary.insert(view.boundary.begin() + 129, view.boundary[128]);
    view.boundary.push_back(view.boundary.front());
    ASSERT_TRUE(FrozenValidityProjection{view}.validGeometryReference());
    compareBounds(RaidVisibilityBoundsQuery{view}, FrozenLinearBoundsQuery{view}, probes(view));
}

TEST(RunnerInfectedVisibilityEquivalenceTest, GroupedBoundsPreserveFarThinSignedZeroAndSeamQueries) {
    using namespace v4_visibility_reference;
    const auto far = regularFan(161, {16777216, 16777216}, 512, {33554432, 33554432});
    ASSERT_TRUE(FrozenValidityProjection{far}.validGeometryReference());
    compareBounds(RaidVisibilityBoundsQuery{far}, FrozenLinearBoundsQuery{far}, probes(far));
    auto zero = regularFan(129, {256, 256});
    zero.observer = {-0.0F, 0.0F};
    ASSERT_TRUE(FrozenValidityProjection{zero}.validGeometryReference());
    compareBounds(RaidVisibilityBoundsQuery{zero}, FrozenLinearBoundsQuery{zero}, probes(zero));
    const auto index = RaidSpaceBlockerIndex::build({3000, 3000}, denseCover());
    ASSERT_TRUE(index);
    auto key = observation({1216, 800}, heading(175), 100, 600);
    key.lightAssetId = 37; key.lightBatteryId = 38;
    key.lightAngleDegrees = 70; key.lightDistance = 1000;
    const auto view = frozenPreV4Cone(FrozenCandidateIndex{*index}, key, nullptr);
    ASSERT_TRUE(FrozenValidityProjection{view}.validGeometryReference());
    compareBounds(RaidVisibilityBoundsQuery{view}, FrozenLinearBoundsQuery{view}, probes(view));
}

TEST(RunnerInfectedVisibilityEquivalenceTest, GroupedBoundsCopyMoveAndSourceMutationKeepOwnedSnapshot) {
    using namespace v4_visibility_reference;
    const auto view = regularFan(161);
    const auto queries = probes(view);
    const FrozenLinearBoundsQuery expected{view};
    auto copied = [&] {
        auto sourceView = view;
        RaidVisibilityBoundsQuery source{sourceView};
        auto result = source;
        sourceView.boundary.clear(); sourceView.observer = {};
        source = RaidVisibilityBoundsQuery{};
        return result;
    }();
    auto moved = [&] {
        auto sourceView = view;
        RaidVisibilityBoundsQuery source{sourceView};
        auto result = std::move(source);
        sourceView.boundary.clear();
        return result;
    }();
    RaidVisibilityBoundsQuery assigned, moveAssigned;
    assigned = copied;
    moveAssigned = std::move(assigned);
    for (const auto *actual : {&copied, &moved, &moveAssigned})
        compareBounds(*actual, expected, queries);
}

TEST(RunnerInfectedVisibilityEquivalenceTest, GroupedBoundsMalformedViewsAndRectanglesMatchFrozenLinear) {
    using namespace v4_visibility_reference;
    const auto view = regularFan(129);
    const float infinity = std::numeric_limits<float>::infinity();
    const float nan = std::numeric_limits<float>::quiet_NaN();
    std::vector<RaidVisibilityProjection> invalid;
    auto bad = view; bad.valid = false; invalid.push_back(bad);
    bad = view; bad.geometryRevision = 0; invalid.push_back(bad);
    bad = view; bad.boundary.resize(2); invalid.push_back(bad);
    bad = view; bad.boundary[64].x = nan; invalid.push_back(bad);
    bad = view; bad.boundary[64].y = infinity; invalid.push_back(bad);
    bad = view; bad.boundary[64].x = -1; invalid.push_back(bad);
    bad = view; bad.observer.x = infinity; invalid.push_back(bad);
    bad = view; std::reverse(bad.boundary.begin(), bad.boundary.end()); invalid.push_back(bad);
    for (const auto &entry : invalid) {
        ASSERT_FALSE(FrozenValidityProjection{entry}.validGeometryReference());
        compareBounds(RaidVisibilityBoundsQuery{entry}, FrozenLinearBoundsQuery{entry}, probes(view));
    }
    const std::vector<Rect> invalidRects{
        {{nan, 0}, {1, 1}}, {{0, infinity}, {1, 1}}, {{0, 0}, {0, 1}},
        {{0, 0}, {1, -1}}, {{0, 0}, {nan, 1}}, {{0, 0}, {1, infinity}}};
    compareBounds(RaidVisibilityBoundsQuery{view}, FrozenLinearBoundsQuery{view}, invalidRects);
    compareBounds(RaidVisibilityBoundsQuery{}, FrozenLinearBoundsQuery{}, probes(view));
}

TEST(RunnerInfectedVisibilityEquivalenceTest, GroupedBoundsMatchFrozenAcrossFourByFourRoundingChanges) {
    using namespace v4_visibility_reference;
    RestoreEnvironment restore;
    ASSERT_EQ(std::fesetround(FE_TONEAREST), 0);
    const auto view = regularFan(129);
    const auto queries = probes(view);
    const std::array modes{FE_TONEAREST, FE_DOWNWARD, FE_UPWARD, FE_TOWARDZERO};
    for (const int construction : modes) {
        ASSERT_EQ(std::fesetround(construction), 0);
        const RaidVisibilityBoundsQuery actual{view};
        const FrozenLinearBoundsQuery expected{view};
        for (const int query : modes) {
            ASSERT_EQ(std::fesetround(query), 0);
            SCOPED_TRACE(testing::Message() << "construct=" << construction << " query=" << query);
            compareBounds(actual, expected, queries);
        }
    }
}

#if defined(_M_IX86) || defined(_M_X64) || defined(__i386__) || defined(__x86_64__)
TEST(RunnerInfectedVisibilityEquivalenceTest, GroupedBoundsMatchFrozenAcrossIndependentSseModes) {
    using namespace v4_visibility_reference;
    RestoreEnvironment restore;
    ASSERT_EQ(std::fesetround(FE_TONEAREST), 0);
    const auto view = regularFan(129);
    auto queries = probes(view);
    const float tiny = std::numeric_limits<float>::denorm_min();
    queries.push_back({{-tiny, tiny}, {tiny, 1}});
    queries.push_back({{view.observer.x, view.observer.y}, {tiny, tiny}});
    constexpr unsigned roundingMask = 3U << 13U;
    constexpr unsigned flushMask = (1U << 15U) | (1U << 6U);
    const unsigned base = _mm_getcsr() & ~(roundingMask | flushMask);
    for (unsigned construction = 0; construction < 4; ++construction) {
        _mm_setcsr(base | (construction << 13U));
        const RaidVisibilityBoundsQuery actual{view};
        const FrozenLinearBoundsQuery expected{view};
        for (unsigned query = 0; query < 4; ++query) {
            _mm_setcsr(base | (query << 13U));
            SCOPED_TRACE(testing::Message() << "SSE construct=" << construction << " query=" << query);
            compareBounds(actual, expected, queries);
        }
    }
    const std::array<unsigned, 4> flushModes{0, 1U << 15U, 1U << 6U, flushMask};
    for (unsigned construction : flushModes) {
        _mm_setcsr(base | construction);
        const RaidVisibilityBoundsQuery actual{view};
        const FrozenLinearBoundsQuery expected{view};
        for (unsigned query : flushModes) {
            _mm_setcsr(base | query);
            SCOPED_TRACE(testing::Message() << "flush construct=" << construction << " query=" << query);
            compareBounds(actual, expected, queries);
        }
    }
}
#endif

TEST(RunnerInfectedVisibilityEquivalenceTest, RelativeConeSlabsPreserveNearZeroAxesAndOriginalWork) {
    using namespace v4_visibility_reference;
    const float threshold = 1e-7F;
    const float infinity = std::numeric_limits<float>::infinity();
    const std::vector<BallisticBlocker> walls{
        {1, {{164, 127}, {0.002F, 20}}}, {2, {{180, 90}, {40, 0.003F}}},
        {3, {{450, 0}, {0.004F, 512}}}, {4, {{0, 400}, {512, 0.006F}}}};
    const auto index = RaidSpaceBlockerIndex::build({512, 512}, walls, 64);
    ASSERT_TRUE(index);
    for (float side : {-0.0F, 0.0F, -threshold, threshold,
             std::nextafter(threshold, 0.0F), std::nextafter(threshold, infinity),
             -std::nextafter(threshold, 0.0F), -std::nextafter(threshold, infinity)})
        for (const Vec2 direction : {Vec2{1, side}, Vec2{side, 1}, Vec2{-1, side}}) {
            SCOPED_TRACE(testing::Message() << "axis=" << direction.x << ',' << direction.y);
            compareCone(*index, observation({128, 128}, direction, 180, 500));
        }
}

TEST(RunnerInfectedVisibilityEquivalenceTest, RelativeConeSlabsPreserveWorldLightSeamsAndCandidateOrder) {
    using namespace v4_visibility_reference;
    const std::vector<BallisticBlocker> walls{
        {1, {{2050, 1400}, {2, 900}}}, {2, {{1500, 1550}, {0.002F, 300}}},
        {3, {{1800, 1850}, {150, 0.003F}}}, {4, {{2250, 1250}, {0.004F, 1000}}},
        {5, {{1600, 2100}, {250, 0.006F}}}};
    for (const float cell : {32.0F, 128.0F, 1024.0F}) {
        const auto index = RaidSpaceBlockerIndex::build({3000, 3000}, walls, cell);
        auto reversed = walls; std::reverse(reversed.begin(), reversed.end());
        const auto reverseIndex = RaidSpaceBlockerIndex::build({3000, 3000}, reversed, cell);
        ASSERT_TRUE(index); ASSERT_TRUE(reverseIndex);
        for (const Vec2 observer : {Vec2{0, 0}, Vec2{1500, 1500}, Vec2{2999, 2999}}) {
            auto key = observation(observer, heading(45), 100, 600);
            if (observer.x > 2000) key.direction = heading(225);
            key.lightAssetId = 91; key.lightBatteryId = 92;
            key.lightAngleDegrees = 70; key.lightDistance = 1000;
            compareCone(*index, key);
            compareCone(*reverseIndex, key);
        }
    }
    const auto far = RaidSpaceBlockerIndex::build({200000, 180000},
        std::vector<BallisticBlocker>{{1, {{100240, 90020}, {0.015625F, 550}}},
            {2, {{100200, 90200}, {600, 0.0078125F}}}}, 4096);
    ASSERT_TRUE(far);
    compareCone(*far, observation({100000, 90000}, heading(35), 170, 1500));
}

TEST(RunnerInfectedVisibilityEquivalenceTest, RelativeConeSlabsFallbackMatchesFrozenFloatingEnvironments) {
    using namespace v4_visibility_reference;
    RestoreEnvironment restore;
    ASSERT_EQ(std::fesetround(FE_TONEAREST), 0);
    const std::vector<BallisticBlocker> walls{
        {1, {{192.015625F, 96.03125F}, {0.0020000003278255462646484375F, 80}}},
        {2, {{240.0625F, 256.015625F}, {170.03125F, 0.0078125F}}}};
    const auto index = RaidSpaceBlockerIndex::build({512, 512}, walls, 64);
    ASSERT_TRUE(index);
    const FrozenCandidateIndex oldIndex{*index};
    auto key = observation({128, 128}, {1, -0.0F}, 110, 450);
    key.lightAssetId = 7; key.lightBatteryId = 8;
    key.lightAngleDegrees = 70; key.lightDistance = 500;
    for (const int mode : {FE_TONEAREST, FE_DOWNWARD, FE_UPWARD, FE_TOWARDZERO}) {
        ASSERT_EQ(std::fesetround(mode), 0);
        SCOPED_TRACE(testing::Message() << "rounding=" << mode);
        compareCone(*index, key, &oldIndex);
    }
#if defined(_M_IX86) || defined(_M_X64) || defined(__i386__) || defined(__x86_64__)
    ASSERT_EQ(std::fesetround(FE_TONEAREST), 0);
    constexpr unsigned roundingMask = 3U << 13U;
    constexpr unsigned flushMask = (1U << 15U) | (1U << 6U);
    const unsigned base = _mm_getcsr() & ~(roundingMask | flushMask);
    for (unsigned rounding = 0; rounding < 4; ++rounding) {
        _mm_setcsr(base | (rounding << 13U));
        compareCone(*index, key, &oldIndex);
    }
    for (unsigned flush : {0U, 1U << 15U, 1U << 6U, flushMask}) {
        _mm_setcsr(base | flush);
        compareCone(*index, key, &oldIndex);
    }
#endif
}
