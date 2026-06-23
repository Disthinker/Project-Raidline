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
