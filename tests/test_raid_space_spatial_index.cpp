#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <vector>

#include "raid_space_query.h"
#include "raid_space_spatial_index.h"
#include "raid_visibility.h"

TEST(RaidSpaceBlockerIndexTest, IndexedLineOfSightMatchesFullScan)
{
    const std::vector<BallisticBlocker> blockers{
        BallisticBlocker{1U, Rect{Vec2{200.0F, 80.0F}, Vec2{60.0F, 260.0F}}},
        BallisticBlocker{2U, Rect{Vec2{500.0F, 300.0F}, Vec2{180.0F, 60.0F}}},
        BallisticBlocker{3U, Rect{Vec2{840.0F, 120.0F}, Vec2{90.0F, 180.0F}}}};
    const auto index = RaidSpaceBlockerIndex::build(
        Vec2{1100.0F, 640.0F}, blockers, 128.0F);
    ASSERT_TRUE(index.has_value());

    const std::vector<std::pair<Vec2, Vec2>> segments{
        {Vec2{40.0F, 40.0F}, Vec2{1040.0F, 40.0F}},
        {Vec2{40.0F, 160.0F}, Vec2{1040.0F, 160.0F}},
        {Vec2{420.0F, 520.0F}, Vec2{760.0F, 120.0F}},
        {Vec2{700.0F, 500.0F}, Vec2{1040.0F, 500.0F}}};
    for (const auto &[start, end] : segments)
    {
        EXPECT_EQ(
            index->hasLineOfSight(start, end),
            raidSpaceHasLineOfSight(start, end, blockers));
    }
}

TEST(RaidSpaceBlockerIndexTest, IndexedLineOfSightMatchesReferenceGridSweep)
{
    std::vector<BallisticBlocker> blockers;
    for (std::uint32_t index{}; index < 48U; ++index)
    {
        blockers.push_back(BallisticBlocker{
            index + 1U,
            Rect{
                Vec2{
                    60.0F + static_cast<float>(index % 8U) * 130.0F,
                    50.0F + static_cast<float>(index / 8U) * 90.0F},
                Vec2{
                    18.0F + static_cast<float>(index % 3U) * 21.0F,
                    20.0F + static_cast<float>(index % 4U) * 17.0F}}});
    }
    const auto index = RaidSpaceBlockerIndex::build(
        Vec2{1100.0F, 640.0F}, blockers, 96.0F);
    ASSERT_TRUE(index.has_value());

    for (int sample{}; sample < 32; ++sample)
    {
        const Vec2 start{
            15.0F + static_cast<float>((sample * 97) % 1060),
            15.0F + static_cast<float>((sample * 53) % 600)};
        const Vec2 end{
            15.0F + static_cast<float>((sample * 211 + 37) % 1060),
            15.0F + static_cast<float>((sample * 139 + 29) % 600)};
        EXPECT_EQ(
            index->hasLineOfSight(start, end),
            raidSpaceHasLineOfSight(start, end, blockers));
    }
}

TEST(RaidSpaceBlockerIndexTest, SweptBoundsReturnStableOriginalOrder)
{
    const std::vector<BallisticBlocker> blockers{
        BallisticBlocker{1U, Rect{Vec2{420.0F, 100.0F}, Vec2{60.0F, 60.0F}}},
        BallisticBlocker{2U, Rect{Vec2{80.0F, 100.0F}, Vec2{60.0F, 60.0F}}},
        BallisticBlocker{3U, Rect{Vec2{260.0F, 100.0F}, Vec2{60.0F, 60.0F}}}};
    const auto index = RaidSpaceBlockerIndex::build(
        Vec2{640.0F, 360.0F}, blockers, 100.0F);
    ASSERT_TRUE(index.has_value());
    std::vector<std::size_t> candidates;

    index->queryCandidateIndices(
        Rect{Vec2{50.0F, 80.0F}, Vec2{480.0F, 100.0F}},
        candidates);

    EXPECT_EQ(candidates, (std::vector<std::size_t>{0U, 1U, 2U}));
}

TEST(RaidSpaceBlockerIndexTest, LocalQueryDoesNotScanSparseWholeSpace)
{
    std::vector<BallisticBlocker> blockers;
    blockers.reserve(100U);
    for (std::size_t index{}; index < 100U; ++index)
    {
        blockers.push_back(BallisticBlocker{
            static_cast<std::uint32_t>(index + 1U),
            Rect{
                Vec2{
                    40.0F + static_cast<float>(index % 10U) * 300.0F,
                    40.0F + static_cast<float>(index / 10U) * 300.0F},
                Vec2{50.0F, 50.0F}}});
    }
    const auto index = RaidSpaceBlockerIndex::build(
        Vec2{3100.0F, 3100.0F}, blockers, 128.0F);
    ASSERT_TRUE(index.has_value());
    std::vector<std::size_t> candidates;

    index->queryCandidateIndices(
        Rect{Vec2{0.0F, 0.0F}, Vec2{120.0F, 120.0F}},
        candidates);

    ASSERT_FALSE(candidates.empty());
    EXPECT_LT(candidates.size(), blockers.size() / 10U);
}

TEST(RaidSpaceBlockerIndexTest, InvalidGeometryFailsClosed)
{
    EXPECT_FALSE(RaidSpaceBlockerIndex::build(Vec2{}, {}).has_value());
    const std::vector<BallisticBlocker> invalid{
        BallisticBlocker{
            1U,
            Rect{
                Vec2{std::numeric_limits<float>::quiet_NaN(), 0.0F},
                Vec2{50.0F, 50.0F}}}};
    EXPECT_FALSE(
        RaidSpaceBlockerIndex::build(
            Vec2{640.0F, 360.0F},
            invalid)
            .has_value());
}

TEST(RaidSpaceBlockerIndexTest, RayAabbRejectionPreservesReferenceEdgesNeighborsAndDegenerateSegments)
{
    const float infinity = std::numeric_limits<float>::infinity();
    const float epsilon = kRaidSightGeometryEpsilon;
    for (const Rect wall : {Rect{{96, 96}, {32, 32}},
             Rect{{128, 96}, {0.5F, 32}}, Rect{{128, 96}, {0.002F, 32}}})
    {
        std::vector<BallisticBlocker> blockers{{1, wall}};
        // These share cells with many rays but have disjoint exact bounds.
        // They exercise the new early rejection without changing the oracle.
        for (std::uint64_t index = 0; index < 64; ++index)
            blockers.push_back({index + 2,
                {{192 + static_cast<float>(index % 8) * 24,
                     192 + static_cast<float>(index / 8) * 24}, {3, 3}}});
        const float left = wall.position.x, right = left + wall.size.x;
        const float top = wall.position.y, bottom = top + wall.size.y;
        const Vec2 center{left + wall.size.x / 2, top + wall.size.y / 2};
        std::vector<std::pair<Vec2, Vec2>> segments{
            {center, center}, // Zero length inside the canonical open interior.
            {{left, top}, {left, top}}, // Zero length on a corner.
            {{left - 1, top - 1}, {left - 1, top - 1}},
            {{8, 8}, {16, 16}}, // Exact AABBs are strictly disjoint.
            {center, {right + 16, center.y}},
            {{left - 16, top - 16}, {right + 16, bottom + 16}},
            {{left - 16, top + 16}, {left + 16, top - 16}}, // Corner tangent.
            {{right - 16, bottom + 16}, {right + 16, bottom - 16}}};
        for (float edge : {left, right, left + epsilon, right - epsilon})
            for (float x : {edge, std::nextafter(edge, -infinity),
                     std::nextafter(edge, infinity), edge - epsilon, edge + epsilon})
                segments.push_back({{x, top - 16}, {x, bottom + 16}});
        for (float edge : {top, bottom, top + epsilon, bottom - epsilon})
            for (float y : {edge, std::nextafter(edge, -infinity),
                     std::nextafter(edge, infinity), edge - epsilon, edge + epsilon})
                segments.push_back({{left - 16, y}, {right + 16, y}});

        for (float cellSize : {32.0F, 96.0F, 128.0F, 1024.0F})
        {
            SCOPED_TRACE(::testing::Message() << "wall=" << left << ',' << top
                << " size=" << wall.size.x << ',' << wall.size.y << " cell=" << cellSize);
            const auto index = RaidSpaceBlockerIndex::build({512, 640}, blockers, cellSize);
            ASSERT_TRUE(index);
            for (const auto &[start, end] : segments)
            {
                SCOPED_TRACE(::testing::Message() << "ray=" << start.x << ',' << start.y
                    << " -> " << end.x << ',' << end.y);
                EXPECT_EQ(index->hasLineOfSight(start, end),
                    raidSpaceHasLineOfSight(start, end, blockers));
                EXPECT_EQ(index->hasLineOfSight(end, start),
                    raidSpaceHasLineOfSight(end, start, blockers));
            }
            if (cellSize == 1024)
            {
                std::size_t examined = 999;
                EXPECT_TRUE(index->hasLineOfSight({8, 8}, {16, 16}, &examined));
                // All walls occupy this one visited cell. AABB skips retain
                // the established candidate-work metric, not predicate calls.
                EXPECT_EQ(examined, blockers.size());
            }
        }
    }
}

TEST(RaidSpaceBlockerIndexTest, LocalCellMergeAndFallbackPreserveEveryOriginalInputIndexInOrder)
{
    for (float cellSize : {32.0F, 96.0F, 128.0F})
        for (std::size_t occupiedCells : {1U, 2U, 8U, 9U})
            for (std::size_t candidateCount : {256U, 257U})
            {
                SCOPED_TRACE(::testing::Message() << "cell=" << cellSize
                    << " occupied=" << occupiedCells << " candidates=" << candidateCount);
                std::vector<BallisticBlocker> blockers;
                std::vector<std::size_t> expected, expectedPartial;
                const std::size_t partialCells = std::min<std::size_t>(2, occupiedCells);
                const auto addExcluded = [&]
                {
                    blockers.push_back({static_cast<std::uint64_t>(blockers.size() + 1),
                        {{(static_cast<float>(occupiedCells) + 0.5F) * cellSize,
                             2.5F * cellSize}, {1, 1}}});
                };
                // Excluded entries precede and interrupt the wanted original
                // indices, so a count or is_sorted assertion cannot mask loss.
                addExcluded();
                for (std::size_t ordinal = 0; ordinal < candidateCount; ++ordinal)
                {
                    if (ordinal == candidateCount / 2) addExcluded();
                    const std::size_t cell = occupiedCells - 1 - ordinal % occupiedCells;
                    const std::size_t originalIndex = blockers.size();
                    expected.push_back(originalIndex);
                    if (cell < partialCells) expectedPartial.push_back(originalIndex);
                    // Reverse cyclic cell placement interleaves sorted ranges:
                    // concatenating cell contents is not the expected order.
                    blockers.push_back({static_cast<std::uint64_t>(originalIndex + 1),
                        {{(static_cast<float>(cell) + 0.5F) * cellSize,
                             0.5F * cellSize}, {1, 1}}});
                }
                addExcluded();
                const Vec2 worldSize{(static_cast<float>(occupiedCells) + 2) * cellSize,
                    4 * cellSize};
                const auto index = RaidSpaceBlockerIndex::build(worldSize, blockers, cellSize);
                ASSERT_TRUE(index);
                std::vector<std::size_t> actual{99999, 88888};
                const Rect fullQuery{{0, 0},
                    {static_cast<float>(occupiedCells) * cellSize - 1, 0.75F * cellSize}};
                index->queryCandidateIndices(fullQuery, actual);
                ASSERT_EQ(actual.size(), candidateCount);
                EXPECT_EQ(actual, expected);
                // Reusing output also verifies that no old range or stack
                // pointer survives the previous query.
                const Rect partialQuery{{0, 0},
                    {static_cast<float>(partialCells) * cellSize - 1, 0.75F * cellSize}};
                index->queryCandidateIndices(partialQuery, actual);
                EXPECT_EQ(actual, expectedPartial);
                index->queryCandidateIndices(fullQuery, actual);
                EXPECT_EQ(actual, expected);
                index->queryCandidateIndices({{0, 3.25F * cellSize}, {1, 1}}, actual);
                EXPECT_TRUE(actual.empty());
            }
}

// The v3 oracle deliberately retains the original indexed traversal rather
// than using the full scan: expanded-cell filtering and first-hit order are
// themselves part of the established indexed result and work-count contract.
#include <array>
#include <cfenv>
#include <cstdint>
#include <utility>

#if defined(_M_IX86) || defined(_M_X64) || defined(__i386__) || defined(__x86_64__)
#include <xmmintrin.h>
#endif

namespace slab_preparation_reference {

bool finite(Vec2 value) {
    return std::isfinite(value.x) && std::isfinite(value.y);
}
bool finitePositive(Vec2 value) {
    return finite(value) && value.x > 0.0F && value.y > 0.0F;
}

// Copied from the pre-v3 index. Do not delegate to prepared slab/ray helpers.
// Geometry tests still use the unchanged public scalar interior predicate.
struct OriginalIndex {
    Vec2 worldSize{};
    float cellSize{};
    std::size_t columns{}, rows{};
    Vec2 maximumHalfExtent{};
    std::vector<Rect> bounds;
    std::vector<std::vector<std::size_t>> cells;

    std::size_t column(float x) const {
        const float raw = std::floor(x / cellSize);
        return static_cast<std::size_t>(std::clamp(raw, 0.0F, static_cast<float>(columns - 1U)));
    }
    std::size_t row(float y) const {
        const float raw = std::floor(y / cellSize);
        return static_cast<std::size_t>(std::clamp(raw, 0.0F, static_cast<float>(rows - 1U)));
    }
    static std::optional<OriginalIndex> build(Vec2 world,
        std::span<const BallisticBlocker> blockers, float size) {
        if (!finitePositive(world) || !std::isfinite(size) || size <= 0.0F) return std::nullopt;
        OriginalIndex index;
        index.worldSize = world;
        index.cellSize = size;
        index.columns = static_cast<std::size_t>(std::max(1.0F, std::ceil(world.x / size)));
        index.rows = static_cast<std::size_t>(std::max(1.0F, std::ceil(world.y / size)));
        index.cells.resize(index.columns * index.rows);
        index.bounds.reserve(blockers.size());
        for (const auto &blocker : blockers) {
            if (!finite(blocker.bounds.position) || !finitePositive(blocker.bounds.size)) return std::nullopt;
            const auto original = index.bounds.size();
            index.bounds.push_back(blocker.bounds);
            index.maximumHalfExtent.x = std::max(index.maximumHalfExtent.x, blocker.bounds.size.x * 0.5F);
            index.maximumHalfExtent.y = std::max(index.maximumHalfExtent.y, blocker.bounds.size.y * 0.5F);
            const Vec2 center{blocker.bounds.position.x + blocker.bounds.size.x * 0.5F,
                blocker.bounds.position.y + blocker.bounds.size.y * 0.5F};
            const auto x = index.column(center.x), y = index.row(center.y);
            index.cells[y * index.columns + x].push_back(original);
        }
        return index;
    }
    bool hasLineOfSight(Vec2 start, Vec2 end, std::size_t *tests) const {
        *tests = 0;
        if (!finite(start) || !finite(end)) return false;
        const float rayLeft = std::min(start.x, end.x), rayRight = std::max(start.x, end.x);
        const float rayTop = std::min(start.y, end.y), rayBottom = std::max(start.y, end.y);
        const float minimumX = std::min(start.x, end.x) - maximumHalfExtent.x;
        const float minimumY = std::min(start.y, end.y) - maximumHalfExtent.y;
        const float maximumX = std::max(start.x, end.x) + maximumHalfExtent.x;
        const float maximumY = std::max(start.y, end.y) + maximumHalfExtent.y;
        const auto firstColumn = column(minimumX), lastColumn = column(maximumX);
        const auto firstRow = row(minimumY), lastRow = row(maximumY);
        for (auto y = firstRow; y <= lastRow; ++y) {
            for (auto x = firstColumn; x <= lastColumn; ++x) {
                const auto &cell = cells[y * columns + x];
                if (cell.empty()) continue;
                const Rect expandedCell{{static_cast<float>(x) * cellSize - maximumHalfExtent.x,
                    static_cast<float>(y) * cellSize - maximumHalfExtent.y},
                    {cellSize + maximumHalfExtent.x * 2.0F, cellSize + maximumHalfExtent.y * 2.0F}};
                if (!raidSpaceSegmentIntersectsBlockerInterior(start, end, expandedCell)) continue;
                for (const auto candidate : cell) {
                    ++*tests;
                    const auto &rect = bounds[candidate];
                    if (rect.position.x + rect.size.x < rayLeft || rect.position.x > rayRight ||
                        rect.position.y + rect.size.y < rayTop || rect.position.y > rayBottom) continue;
                    if (raidSpaceSegmentIntersectsBlockerInterior(start, end, rect)) return false;
                }
            }
        }
        return true;
    }
};

using Segment = std::pair<Vec2, Vec2>;

void compare(const RaidSpaceBlockerIndex &actual, const OriginalIndex &original,
             Vec2 start, Vec2 end) {
    SCOPED_TRACE(testing::Message() << "ray=" << start.x << ',' << start.y << "->" << end.x << ',' << end.y);
    std::size_t expectedCount = 999999, actualCount = 888888;
    const bool expected = original.hasLineOfSight(start, end, &expectedCount);
    EXPECT_EQ(actual.hasLineOfSight(start, end, &actualCount), expected);
    EXPECT_EQ(actualCount, expectedCount);
    EXPECT_EQ(actual.hasLineOfSight(start, end), expected);
}

void compareSegments(Vec2 world, const std::vector<BallisticBlocker> &blockers, float cell,
                     const std::vector<Segment> &segments) {
    SCOPED_TRACE(testing::Message() << "cell=" << cell << " walls=" << blockers.size());
    const auto actual = RaidSpaceBlockerIndex::build(world, blockers, cell);
    const auto original = OriginalIndex::build(world, blockers, cell);
    ASSERT_EQ(actual.has_value(), original.has_value());
    ASSERT_TRUE(actual);
    for (const auto &[start, end] : segments) {
        compare(*actual, *original, start, end);
        compare(*actual, *original, end, start);
    }
}

std::vector<Segment> edgeSegments(Rect wall) {
    const auto infinity = std::numeric_limits<float>::infinity();
    const float x = wall.position.x, y = wall.position.y;
    const float right = x + wall.size.x, bottom = y + wall.size.y;
    const Vec2 center{x + wall.size.x * 0.5F, y + wall.size.y * 0.5F};
    std::vector<Segment> segments{{center, center}, {{x, y}, {x, y}},
        {{x - 2, y - 2}, {right + 2, bottom + 2}}, {{x - 2, bottom + 2}, {right + 2, y - 2}}};
    for (float edge : {x, x + kRaidSightGeometryEpsilon, right - kRaidSightGeometryEpsilon, right})
        for (float value : {std::nextafter(edge, -infinity), edge, std::nextafter(edge, infinity)})
            segments.push_back({{value, y - 2}, {value, bottom + 2}});
    for (float edge : {y, y + kRaidSightGeometryEpsilon, bottom - kRaidSightGeometryEpsilon, bottom})
        for (float value : {std::nextafter(edge, -infinity), edge, std::nextafter(edge, infinity)})
            segments.push_back({{x - 2, value}, {right + 2, value}});
    return segments;
}

struct RestoreFloatingEnvironment {
    std::fenv_t environment{};
#if defined(_M_IX86) || defined(_M_X64) || defined(__i386__) || defined(__x86_64__)
    unsigned mxcsr = _mm_getcsr();
#endif
    RestoreFloatingEnvironment() { std::fegetenv(&environment); }
    ~RestoreFloatingEnvironment() {
        std::fesetenv(&environment);
#if defined(_M_IX86) || defined(_M_X64) || defined(__i386__) || defined(__x86_64__)
        _mm_setcsr(mxcsr);
#endif
    }
};

} // namespace slab_preparation_reference

TEST(RaidSpaceBlockerIndexTest, PreparedSlabsPreserveNextafterSignedZeroAndNearZeroAxes) {
    using namespace slab_preparation_reference;
    const float epsilon = kRaidSightGeometryEpsilon;
    const float infinity = std::numeric_limits<float>::infinity();
    for (const Rect wall : {Rect{{32, 32}, {32, 32}}, Rect{{-0.0F, -0.0F}, {4, 4}},
            Rect{{64, 32}, {epsilon * 2, 32}}, Rect{{64, 32}, {std::nextafter(epsilon * 2, infinity), 32}}}) {
        std::vector<BallisticBlocker> blockers{{1, wall}, {2, {{192, 224}, {15, 11}}}};
        auto segments = edgeSegments(wall);
        for (float dx : {-0.0F, 0.0F, -epsilon, epsilon,
                 std::nextafter(epsilon, 0.0F), std::nextafter(epsilon, infinity),
                 -std::nextafter(epsilon, 0.0F), -std::nextafter(epsilon, infinity)}) {
            segments.push_back({{wall.position.x, wall.position.y - 1},
                {wall.position.x + dx, wall.position.y + wall.size.y + 1}});
            segments.push_back({{wall.position.x - 1, wall.position.y},
                {wall.position.x + wall.size.x + 1, wall.position.y + dx}});
        }
        for (float cell : {16.0F, 64.0F, 96.5F, 512.0F})
            compareSegments({512, 384}, blockers, cell, segments);
    }
}

TEST(RaidSpaceBlockerIndexTest, PreparedSlabsPreserveTinyFarAndOverflowGeometry) {
    using namespace slab_preparation_reference;
    const float maximum = std::numeric_limits<float>::max();
    const float denormal = std::numeric_limits<float>::denorm_min();
    const std::array<Rect, 6> walls{
        Rect{{32, 64}, {denormal, 10}}, Rect{{-denormal, -0.0F}, {4, 4}},
        Rect{{16777216.0F, 16777216.0F}, {16, 32}},
        Rect{{-16777216.0F, -16777216.0F}, {32, 16}},
        Rect{{maximum * 0.75F, 0}, {maximum * 0.5F, 64}},
        Rect{{0, maximum * 0.75F}, {64, maximum * 0.5F}}};
    for (const auto &wall : walls) {
        auto segments = edgeSegments(wall);
        segments.push_back({{-maximum, 32}, {maximum, 32}});
        segments.push_back({{32, -maximum}, {32, maximum}});
        segments.push_back({{-maximum, -maximum}, {maximum, maximum}});
        segments.push_back({{-0.0F, 0.0F}, {0.0F, -0.0F}});
        const std::vector<BallisticBlocker> blockers{{1, wall}, {2, {{160, 160}, {8, 8}}}};
        for (float cell : {32.0F, 96.0F, 1024.0F})
            compareSegments({512, 384}, blockers, cell, segments);
    }
}

TEST(RaidSpaceBlockerIndexTest, PreparedCellsUseFinalGlobalHalfExtentAndOriginalBoundaries) {
    using namespace slab_preparation_reference;
    const std::vector<BallisticBlocker> blockers{
        {1, {{250, 52}, {1, 1}}}, {2, {{5, 280}, {1, 1}}},
        {3, {{310, 20}, {1, 1}}}, {4, {{-70, 100}, {310, 20}}},
        {5, {{210, -80}, {14, 280}}}, {6, {{520, 410}, {50, 50}}}};
    const float infinity = std::numeric_limits<float>::infinity();
    for (float cell : {16.0F, 63.5F, 64.0F, 96.0F, 128.0F, 1024.0F}) {
        std::vector<Segment> segments;
        for (float boundary : {0.0F, cell, cell * 2, cell * 3, 512.0F})
            for (float v : {std::nextafter(boundary, -infinity), boundary, std::nextafter(boundary, infinity)}) {
                segments.push_back({{v, -100}, {v, 500}});
                segments.push_back({{-100, v}, {650, v}});
                segments.push_back({{v, v}, {v, v}});
            }
        compareSegments({512, 384}, blockers, cell, segments);
    }
}

TEST(RaidSpaceBlockerIndexTest, PreparedIndexRetainsInvalidBuildNonfiniteRayAndEmptyIndexContract) {
    using namespace slab_preparation_reference;
    const float infinity = std::numeric_limits<float>::infinity();
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const std::vector<BallisticBlocker> normal{{1, {{32, 32}, {20, 20}}}};
    for (Vec2 world : {Vec2{}, Vec2{-1, 512}, Vec2{512, 0}, Vec2{nan, 512}, Vec2{512, infinity}})
        EXPECT_FALSE(RaidSpaceBlockerIndex::build(world, normal));
    for (float cell : {0.0F, -0.0F, -1.0F, infinity, nan})
        EXPECT_FALSE(RaidSpaceBlockerIndex::build({512, 384}, normal, cell));
    for (Rect invalid : {Rect{{nan, 0}, {1, 1}}, Rect{{0, infinity}, {1, 1}},
             Rect{{0, 0}, {0, 1}}, Rect{{0, 0}, {1, -1}}, Rect{{0, 0}, {nan, 1}},
             Rect{{0, 0}, {1, infinity}}})
        EXPECT_FALSE(RaidSpaceBlockerIndex::build({512, 384}, std::vector<BallisticBlocker>{{1, invalid}}));
    for (const auto &blockers : {std::vector<BallisticBlocker>{}, normal}) {
        std::vector<Segment> segments{{{1, 1}, {1, 1}}, {{-0.0F, 0}, {0, -0.0F}}, {{0, 20}, {500, 20}}};
        for (Vec2 invalid : {Vec2{nan, 0}, Vec2{infinity, 1}, Vec2{1, -infinity}}) {
            segments.push_back({invalid, {1, 1}});
            segments.push_back({{1, 1}, invalid});
        }
        compareSegments({512, 384}, blockers, 64, segments);
    }
}

TEST(RaidSpaceBlockerIndexTest, PreparedStorageSurvivesIndexCopyMoveAndSourceLifetime) {
    using namespace slab_preparation_reference;
    const std::vector<BallisticBlocker> blockers{
        {1, {{32, 32}, {30, 14}}}, {2, {{256, 170}, {11, 90}}}, {3, {{70, 210}, {100, 6}}}};
    const auto original = OriginalIndex::build({512, 384}, blockers, 64);
    ASSERT_TRUE(original);
    auto segments = edgeSegments(blockers[0].bounds);
    segments.push_back({{0, 0}, {500, 350}});
    auto copied = [&] {
        auto source = RaidSpaceBlockerIndex::build({512, 384}, blockers, 64);
        return RaidSpaceBlockerIndex{*source};
    }();
    auto moved = [&] {
        auto source = RaidSpaceBlockerIndex::build({512, 384}, blockers, 64);
        return RaidSpaceBlockerIndex{std::move(*source)};
    }();
    auto assigned = *RaidSpaceBlockerIndex::build({1, 1}, {}, 1);
    assigned = copied;
    auto moveAssigned = *RaidSpaceBlockerIndex::build({1, 1}, {}, 1);
    moveAssigned = std::move(assigned);
    for (const auto &[start, end] : segments)
        for (const auto *index : {&copied, &moved, &moveAssigned})
            compare(*index, *original, start, end);
    EXPECT_EQ(copied.blockerCount(), blockers.size());
    EXPECT_EQ(moved.allBlockerBounds().size(), blockers.size());
}

TEST(RaidSpaceBlockerIndexTest, PreparedTraversalPreservesFirstHitAndSkippedCandidateWorkCounts) {
    using namespace slab_preparation_reference;
    std::vector<BallisticBlocker> blockers;
    for (std::uint64_t n = 0; n < 45; ++n) {
        // Reverse/interleaved placement means input order and cell order differ.
        const float x = 24 + static_cast<float>((44 - n) % 9) * 49;
        const float y = 20 + static_cast<float>((n * 7) % 5) * 58;
        blockers.push_back({n + 1, {{x, y}, {2 + static_cast<float>(n % 3) * 17, 3 + static_cast<float>(n % 4) * 11}}});
    }
    std::vector<Segment> segments{{{0, 10}, {511, 10}}, {{2, 2}, {3, 3}},
        {{0, 28}, {511, 28}}, {{400, 300}, {20, 20}}};
    for (int n = 0; n < 96; ++n)
        segments.push_back({{static_cast<float>((n * 31) % 512), static_cast<float>((n * 13) % 384)},
            {static_cast<float>((n * 97 + 7) % 512), static_cast<float>((n * 53 + 19) % 384)}});
    for (float cell : {24.0F, 64.0F, 96.5F, 128.0F, 1024.0F})
        compareSegments({512, 384}, blockers, cell, segments);
    const auto index = RaidSpaceBlockerIndex::build({512, 384}, blockers, 1024);
    ASSERT_TRUE(index);
    std::size_t count = 999;
    EXPECT_TRUE(index->hasLineOfSight({2, 2}, {3, 3}, &count));
    EXPECT_EQ(count, blockers.size());
}

TEST(RaidSpaceBlockerIndexTest, PreparedSlabsRemainEquivalentAcrossFloatingRoundingModeChanges) {
    using namespace slab_preparation_reference;
    RestoreFloatingEnvironment restore;
    ASSERT_EQ(std::fesetround(FE_TONEAREST), 0);
    const std::vector<BallisticBlocker> blockers{
        {1, {{32.015625F, 32.03125F}, {32.00390625F, 15.998046875F}}},
        {2, {{95.5F, 64.125F}, {0.0020000003278255462646484375F, 40}}},
        {3, {{240.0625F, 128.015625F}, {170.03125F, 20.015625F}}}};
    auto segments = edgeSegments(blockers[0].bounds);
    const auto thinSegments = edgeSegments(blockers[1].bounds);
    segments.insert(segments.end(), thinSegments.begin(), thinSegments.end());
    segments.push_back({{0, 0}, {500, 300}});
    const std::array modes{FE_TONEAREST, FE_DOWNWARD, FE_UPWARD, FE_TOWARDZERO};
    for (const int buildMode : modes) {
        ASSERT_EQ(std::fesetround(buildMode), 0);
        const auto actual = RaidSpaceBlockerIndex::build({512, 384}, blockers, 63.5F);
        const auto original = OriginalIndex::build({512, 384}, blockers, 63.5F);
        ASSERT_TRUE(actual); ASSERT_TRUE(original);
        for (const int queryMode : modes) {
            ASSERT_EQ(std::fesetround(queryMode), 0);
            SCOPED_TRACE(testing::Message() << "build rounding=" << buildMode << " query rounding=" << queryMode);
            for (const auto &[start, end] : segments) compare(*actual, *original, start, end);
        }
    }
#if defined(_M_IX86) || defined(_M_X64) || defined(__i386__) || defined(__x86_64__)
    // Changing only SSE rounding can leave the x87 state read by fegetround
    // untouched. Cache validation must cover the actual arithmetic mode too.
    ASSERT_EQ(std::fesetround(FE_TONEAREST), 0);
    constexpr unsigned roundingMask = 3U << 13U;
    const unsigned base = _mm_getcsr() & ~roundingMask;
    for (unsigned buildRounding = 0; buildRounding < 4; ++buildRounding) {
        _mm_setcsr(base | (buildRounding << 13U));
        const auto actual = RaidSpaceBlockerIndex::build({512, 384}, blockers, 63.5F);
        const auto original = OriginalIndex::build({512, 384}, blockers, 63.5F);
        ASSERT_TRUE(actual); ASSERT_TRUE(original);
        for (unsigned queryRounding = 0; queryRounding < 4; ++queryRounding) {
            _mm_setcsr(base | (queryRounding << 13U));
            SCOPED_TRACE(testing::Message() << "build SSE rounding=" << buildRounding
                << " query SSE rounding=" << queryRounding);
            for (const auto &[start, end] : segments) compare(*actual, *original, start, end);
        }
    }
#endif
}

#if defined(_M_IX86) || defined(_M_X64) || defined(__i386__) || defined(__x86_64__)
TEST(RaidSpaceBlockerIndexTest, PreparedSlabsRemainEquivalentAcrossFlushAndDenormalModeChanges) {
    using namespace slab_preparation_reference;
    RestoreFloatingEnvironment restore;
    ASSERT_EQ(std::fesetround(FE_TONEAREST), 0);
    constexpr unsigned modeMask = (1U << 15U) | (1U << 6U); // MXCSR FTZ and DAZ.
    const unsigned base = _mm_getcsr() & ~modeMask;
    _mm_setcsr(base);
    const float tiny = std::numeric_limits<float>::denorm_min();
    const std::array<unsigned, 4> modes{0, 1U << 15U, 1U << 6U, modeMask};
    const std::array<std::vector<BallisticBlocker>, 2> scenes{
        std::vector<BallisticBlocker>{{1, {{32, 32}, {40, 20}}}, {2, {{128, 64}, {3, 80}}}},
        std::vector<BallisticBlocker>{{1, {{0, 0}, {tiny, 1}}}, {2, {{32, 32}, {40, 20}}}}};
    const std::vector<Segment> segments{{{-1, 0.5F}, {1, 0.5F}}, {{-tiny, 0}, {tiny, -0.0F}},
        {{tiny, 1}, {tiny, 100}}, {{1, tiny}, {200, -tiny}}, {{0, 0}, {200, 200}},
        {{32.001F, 0}, {32.001F, 200}}, {{-0.0F, 0}, {0, -0.0F}}};
    for (const auto &blockers : scenes)
        for (const unsigned buildMode : modes) {
            _mm_setcsr(base | buildMode);
            const auto actual = RaidSpaceBlockerIndex::build({512, 384}, blockers, 64);
            const auto original = OriginalIndex::build({512, 384}, blockers, 64);
            ASSERT_EQ(actual.has_value(), original.has_value());
            if (!actual) continue; // Both old and new builders reject flushed zero widths.
            for (const unsigned queryMode : modes) {
                _mm_setcsr(base | queryMode);
                SCOPED_TRACE(testing::Message() << "build MXCSR=" << buildMode << " query MXCSR=" << queryMode);
                for (const auto &[start, end] : segments) compare(*actual, *original, start, end);
            }
        }
}
#endif

namespace direct_merge_reference {
std::vector<std::size_t> candidates(const slab_preparation_reference::OriginalIndex &index,
                                    Rect bounds) {
    std::vector<std::size_t> result;
    if (!slab_preparation_reference::finite(bounds.position) ||
        !slab_preparation_reference::finite(bounds.size) ||
        bounds.size.x < 0 || bounds.size.y < 0 || index.cells.empty()) return result;
    const auto firstColumn = index.column(bounds.position.x - index.maximumHalfExtent.x);
    const auto lastColumn = index.column(bounds.position.x + bounds.size.x + index.maximumHalfExtent.x);
    const auto firstRow = index.row(bounds.position.y - index.maximumHalfExtent.y);
    const auto lastRow = index.row(bounds.position.y + bounds.size.y + index.maximumHalfExtent.y);
    for (auto row = firstRow; row <= lastRow; ++row)
        for (auto column = firstColumn; column <= lastColumn; ++column) {
            const auto &cell = index.cells[row * index.columns + column];
            result.insert(result.end(), cell.begin(), cell.end());
        }
    std::sort(result.begin(), result.end());
    return result;
}
}

TEST(RaidSpaceBlockerIndexTest, DirectMergeFallbackInEighthCellRetainsEarlierRangesAndCrossRowOrder) {
    constexpr float cell = 32;
    const std::array<std::size_t, 9> counts{1, 1, 1, 1, 1, 1, 1, 250, 3};
    std::vector<BallisticBlocker> walls;
    for (std::size_t n = 0; n < 250; ++n)
        for (std::size_t reverse = 0; reverse < counts.size(); ++reverse) {
            const auto bin = counts.size() - 1 - reverse;
            if (n >= counts[bin]) continue;
            const auto x = bin % 3, y = bin / 3;
            walls.push_back({static_cast<std::uint64_t>(walls.size() + 1),
                {{static_cast<float>(x) * cell + 14 + static_cast<float>(n % 3) * 0.125F,
                  static_cast<float>(y) * cell + 14}, {1, 1}}});
        }
    const auto actual = RaidSpaceBlockerIndex::build({160, 160}, walls, cell);
    const auto old = slab_preparation_reference::OriginalIndex::build({160, 160}, walls, cell);
    ASSERT_TRUE(actual); ASSERT_TRUE(old);
    std::vector<std::size_t> output{999999, 888888};
    for (const Rect query : {Rect{{0, 0}, {96, 96}}, Rect{{0, 0}, {60, 60}},
             Rect{{65, 65}, {5, 5}}, Rect{{0, 32}, {96, 64}},
             Rect{{128, 128}, {2, 2}}, Rect{{0, 0}, {96, 96}}}) {
        actual->queryCandidateIndices(query, output);
        EXPECT_EQ(output, direct_merge_reference::candidates(*old, query));
    }
}

TEST(RaidSpaceBlockerIndexTest, DirectMergeSingleLargeCellAndInvalidReuseKeepOriginalOutputContract) {
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float infinity = std::numeric_limits<float>::infinity();
    for (std::size_t count : {0U, 1U, 256U, 257U, 600U}) {
        SCOPED_TRACE(testing::Message() << "single cell size=" << count);
        std::vector<BallisticBlocker> walls;
        for (std::size_t n = 0; n < count; ++n)
            walls.push_back({static_cast<std::uint64_t>(n + 1),
                {{12 + static_cast<float>(n % 8) * 0.125F, 12}, {1, 1}}});
        const auto actual = RaidSpaceBlockerIndex::build({128, 128}, walls, 32);
        const auto old = slab_preparation_reference::OriginalIndex::build({128, 128}, walls, 32);
        ASSERT_TRUE(actual); ASSERT_TRUE(old);
        std::vector<std::size_t> output(700, 123456);
        for (const Rect query : {Rect{{0, 0}, {31, 31}}, Rect{{nan, 0}, {1, 1}},
                 Rect{{0, 0}, {-1, 1}}, Rect{{0, 0}, {infinity, 1}},
                 Rect{{96, 96}, {1, 1}}, Rect{{0, 0}, {31, 31}},
                 Rect{{12, 12}, {-0.0F, 0.0F}}}) {
            actual->queryCandidateIndices(query, output);
            EXPECT_EQ(output, direct_merge_reference::candidates(*old, query));
        }
    }
}
