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
