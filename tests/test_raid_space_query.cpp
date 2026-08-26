#include <gtest/gtest.h>

#include <cmath>
#include <utility>
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

TEST(RaidSpaceQueryTest, GoalToleranceRoutesToTargetFlushAgainstCover)
{
    const std::vector<BallisticBlocker> blockers{
        BallisticBlocker{
            1U,
            Rect{Vec2{310.0F, 150.0F}, Vec2{20.0F, 200.0F}}}};

    const std::optional<Vec2> waypoint = nextRaidSpaceWaypoint(
        RaidSpaceNavigationQuery{
            Vec2{385.0F, 266.0F},
            Vec2{294.0F, 266.0F},
            Vec2{50.0F, 50.0F},
            Vec2{800.0F, 600.0F},
            blockers,
            2.0F,
            20.0F});

    ASSERT_TRUE(waypoint.has_value());
    EXPECT_TRUE(
        waypoint->y <= 123.0F + kTolerance ||
        waypoint->y >= 377.0F - kTolerance);
    EXPECT_GT(
        std::hypot(
            waypoint->x - 385.0F,
            waypoint->y - 266.0F),
        kTolerance);
    EXPECT_FALSE(pointInside(*waypoint, blockers.front().bounds));
}

TEST(RaidSpaceQueryTest, PreparedFieldMatchesOneShotMovingQueries)
{
    const std::vector<BallisticBlocker> blockers{
        BallisticBlocker{
            1,
            Rect{Vec2{300.0F, 80.0F}, Vec2{80.0F, 300.0F}}},
        BallisticBlocker{
            2,
            Rect{Vec2{620.0F, 300.0F}, Vec2{180.0F, 70.0F}}}};
    constexpr Vec2 actorSize{50.0F, 50.0F};
    constexpr Vec2 worldSize{960.0F, 640.0F};
    const std::optional<RaidSpaceNavigationField> field =
        RaidSpaceNavigationField::build(
            actorSize,
            worldSize,
            blockers,
            2.0F);
    ASSERT_TRUE(field.has_value());
    EXPECT_FLOAT_EQ(field->actorSize().x, actorSize.x);
    EXPECT_FLOAT_EQ(field->worldSize().y, worldSize.y);

    const std::vector<std::pair<Vec2, Vec2>> queries{
        {Vec2{100.0F, 120.0F}, Vec2{860.0F, 120.0F}},
        {Vec2{100.0F, 520.0F}, Vec2{860.0F, 220.0F}},
        {Vec2{520.0F, 120.0F}, Vec2{860.0F, 520.0F}}};
    for (const auto &[start, goal] : queries)
    {
        const std::optional<Vec2> prepared =
            field->nextWaypoint(start, goal, 20.0F);
        const std::optional<Vec2> oneShot = nextRaidSpaceWaypoint(
            RaidSpaceNavigationQuery{
                start,
                goal,
                actorSize,
                worldSize,
                blockers,
                2.0F,
                20.0F});
        ASSERT_EQ(prepared.has_value(), oneShot.has_value());
        if (prepared.has_value())
        {
            EXPECT_FLOAT_EQ(prepared->x, oneShot->x);
            EXPECT_FLOAT_EQ(prepared->y, oneShot->y);
        }
    }
}

TEST(RaidSpaceQueryTest, InvalidPreparedFieldFailsClosed)
{
    EXPECT_FALSE(
        RaidSpaceNavigationField::build(
            Vec2{0.0F, 50.0F},
            Vec2{960.0F, 640.0F},
            {},
            2.0F)
            .has_value());
}
