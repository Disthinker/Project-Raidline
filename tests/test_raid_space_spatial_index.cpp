#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <limits>
#include <vector>

#include "raid_space_query.h"
#include "raid_space_spatial_index.h"

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
