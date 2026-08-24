#include <gtest/gtest.h>
#include "collision.h"

// 明确重叠：true
TEST(ProjectRaidline, collisionWhileXY)
{
    Rect rect1 = {Vec2{0.0f, 0.0f}, Vec2{5.0f, 5.0f}};
    Rect rect2 = {Vec2{3.0f, 3.0f}, Vec2{5.0f, 5.0f}};

    EXPECT_TRUE(isCollision(rect1, rect2));
}

// X 轴分离：false
TEST(ProjectRaidline, notCollisionWhileXDivided)
{
    Rect rect1 = {Vec2{0.0f, 0.0f}, Vec2{5.0f, 5.0f}};
    Rect rect2 = {Vec2{6.0f, 3.0f}, Vec2{5.0f, 5.0f}};

    EXPECT_FALSE(isCollision(rect1, rect2));
}

// Y 轴分离：false
TEST(ProjectRaidline, notCollisionWhileYDivided)
{
    Rect rect1 = {Vec2{0.0f, 0.0f}, Vec2{5.0f, 5.0f}};
    Rect rect2 = {Vec2{3.0f, 6.0f}, Vec2{5.0f, 5.0f}};

    EXPECT_FALSE(isCollision(rect1, rect2));
}

// 边缘相接：false
TEST(ProjectRaidline, notCollisionWhileBoundary)
{
    Rect rect1 = {Vec2{0.0f, 0.0f}, Vec2{5.0f, 5.0f}};
    Rect rect2 = {Vec2{5.0f, 3.0f}, Vec2{5.0f, 5.0f}};

    EXPECT_FALSE(isCollision(rect1, rect2));
}

// 一个矩形包含另一个矩形：true
TEST(ProjectRaidline, collisionWhileContainment)
{
    Rect rect1 = {Vec2{0.0f, 0.0f}, Vec2{5.0f, 5.0f}};
    Rect rect2 = {Vec2{0.0f, 0.0f}, Vec2{3.0f, 3.0f}};

    EXPECT_TRUE(isCollision(rect1, rect2));
}

// 完全相同：true
TEST(ProjectRaidline, collisionWhileOverlap)
{
    Rect rect1 = {Vec2{0.0f, 0.0f}, Vec2{5.0f, 5.0f}};
    Rect rect2 = {Vec2{0.0f, 0.0f}, Vec2{5.0f, 5.0f}};

    EXPECT_TRUE(isCollision(rect1, rect2));
}

// 部分重叠：true
TEST(ProjectRaidline, collisionWhilePartialOverlap)
{
    Rect rect1 = {Vec2{3.0f, 3.0f}, Vec2{5.0f, 5.0f}};
    Rect rect2 = {Vec2{0.0f, 0.0f}, Vec2{5.0f, 5.0f}};

    EXPECT_TRUE(isCollision(rect1, rect2));
}

TEST(ProjectRaidline, sweptHorizontalCollisionStopsFromBothDirections)
{
    const Rect obstacle{Vec2{100.0F, 40.0F}, Vec2{20.0F, 80.0F}};
    const Rect actorOnLeft{Vec2{20.0F, 60.0F}, Vec2{16.0F, 16.0F}};
    const Rect actorOnRight{Vec2{180.0F, 60.0F}, Vec2{16.0F, 16.0F}};

    EXPECT_FLOAT_EQ(
        resolveHorizontalCollision(actorOnLeft, 160.0F, obstacle),
        84.0F);
    EXPECT_FLOAT_EQ(
        resolveHorizontalCollision(actorOnRight, 40.0F, obstacle),
        120.0F);
}

TEST(ProjectRaidline, sweptVerticalCollisionStopsFromBothDirections)
{
    const Rect obstacle{Vec2{40.0F, 100.0F}, Vec2{80.0F, 20.0F}};
    const Rect actorAbove{Vec2{60.0F, 20.0F}, Vec2{16.0F, 16.0F}};
    const Rect actorBelow{Vec2{60.0F, 180.0F}, Vec2{16.0F, 16.0F}};

    EXPECT_FLOAT_EQ(
        resolveVerticalCollision(actorAbove, 160.0F, obstacle),
        84.0F);
    EXPECT_FLOAT_EQ(
        resolveVerticalCollision(actorBelow, 40.0F, obstacle),
        120.0F);
}

TEST(ProjectRaidline, sweptCollisionAllowsParallelMovementAtTouchingEdge)
{
    const Rect obstacle{Vec2{100.0F, 40.0F}, Vec2{20.0F, 80.0F}};
    const Rect actorTouchingLeft{Vec2{84.0F, 60.0F}, Vec2{16.0F, 16.0F}};

    EXPECT_FLOAT_EQ(
        resolveVerticalCollision(actorTouchingLeft, 100.0F, obstacle),
        100.0F);
}
