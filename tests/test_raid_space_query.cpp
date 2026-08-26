#include <gtest/gtest.h>

#include <vector>

#include "raid_space_query.h"

namespace
{
    constexpr float kTolerance{0.001F};

    bool pointInside(const Vec2 point, const Rect &rect)
    {
        return point.x > rect.position.x &&
               point.x < rect.position.x + rect.size.x &&
               point.y > rect.position.y &&
               point.y < rect.position.y + rect.size.y;
    }
}

TEST(RaidSpaceQueryTest, LineOfSightUsesPublishedBlockerInterior)
{
    const std::vector<BallisticBlocker> blockers{
        BallisticBlocker{
            1U,
            Rect{Vec2{90.0F, 40.0F}, Vec2{20.0F, 80.0F}}}};

    EXPECT_FALSE(raidSpaceHasLineOfSight(
        Vec2{40.0F, 80.0F},
        Vec2{160.0F, 80.0F},
        blockers));
    EXPECT_TRUE(raidSpaceHasLineOfSight(
        Vec2{40.0F, 20.0F},
        Vec2{160.0F, 20.0F},
        blockers));
    EXPECT_TRUE(raidSpaceHasLineOfSight(
        Vec2{40.0F, 40.0F},
        Vec2{160.0F, 40.0F},
        blockers));
}

TEST(RaidSpaceQueryTest, ClearRouteReturnsGoal)
{
    const std::vector<BallisticBlocker> blockers;
    const Vec2 goal{300.0F, 200.0F};

    const std::optional<Vec2> waypoint = nextRaidSpaceWaypoint(
        RaidSpaceNavigationQuery{
            Vec2{60.0F, 60.0F},
            goal,
            Vec2{40.0F, 40.0F},
            Vec2{400.0F, 300.0F},
            blockers});

    ASSERT_TRUE(waypoint.has_value());
    EXPECT_FLOAT_EQ(waypoint->x, goal.x);
    EXPECT_FLOAT_EQ(waypoint->y, goal.y);
}

TEST(RaidSpaceQueryTest, SingleWallReturnsStableCornerWaypoint)
{
    const std::vector<BallisticBlocker> blockers{
        BallisticBlocker{
            1U,
            Rect{Vec2{150.0F, 70.0F}, Vec2{40.0F, 160.0F}}}};
    const RaidSpaceNavigationQuery query{
        Vec2{80.0F, 150.0F},
        Vec2{280.0F, 150.0F},
        Vec2{40.0F, 40.0F},
        Vec2{360.0F, 300.0F},
        blockers};

    const std::optional<Vec2> first = nextRaidSpaceWaypoint(query);
    const std::optional<Vec2> second = nextRaidSpaceWaypoint(query);

    ASSERT_TRUE(first.has_value());
    ASSERT_TRUE(second.has_value());
    EXPECT_NEAR(first->x, second->x, kTolerance);
    EXPECT_NEAR(first->y, second->y, kTolerance);
    EXPECT_LT(first->x, 150.0F);
    EXPECT_TRUE(first->y < 70.0F || first->y > 230.0F);
}

TEST(RaidSpaceQueryTest, MultipleWallsProduceReachableFirstStep)
{
    const std::vector<BallisticBlocker> blockers{
        BallisticBlocker{
            1U,
            Rect{Vec2{130.0F, 70.0F}, Vec2{40.0F, 150.0F}}},
        BallisticBlocker{
            2U,
            Rect{Vec2{220.0F, 110.0F}, Vec2{40.0F, 150.0F}}}};

    const std::optional<Vec2> waypoint = nextRaidSpaceWaypoint(
        RaidSpaceNavigationQuery{
            Vec2{60.0F, 150.0F},
            Vec2{330.0F, 170.0F},
            Vec2{32.0F, 32.0F},
            Vec2{400.0F, 320.0F},
            blockers});

    ASSERT_TRUE(waypoint.has_value());
    EXPECT_TRUE(raidSpaceHasLineOfSight(
        Vec2{60.0F, 150.0F},
        *waypoint,
        blockers));
    for (const BallisticBlocker &blocker : blockers)
    {
        EXPECT_FALSE(pointInside(*waypoint, blocker.bounds));
    }
}

TEST(RaidSpaceQueryTest, SealedGoalFailsClosed)
{
    const std::vector<BallisticBlocker> blockers{
        BallisticBlocker{1U, Rect{Vec2{140.0F, 80.0F}, Vec2{20.0F, 160.0F}}},
        BallisticBlocker{2U, Rect{Vec2{240.0F, 80.0F}, Vec2{20.0F, 160.0F}}},
        BallisticBlocker{3U, Rect{Vec2{140.0F, 80.0F}, Vec2{120.0F, 20.0F}}},
        BallisticBlocker{4U, Rect{Vec2{140.0F, 220.0F}, Vec2{120.0F, 20.0F}}}};

    EXPECT_FALSE(nextRaidSpaceWaypoint(
        RaidSpaceNavigationQuery{
            Vec2{60.0F, 160.0F},
            Vec2{200.0F, 160.0F},
            Vec2{32.0F, 32.0F},
            Vec2{360.0F, 320.0F},
            blockers}).has_value());
}
