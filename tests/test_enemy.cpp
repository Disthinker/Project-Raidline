#include <gtest/gtest.h>
#include "enemy.h"

// 1. 构造后能保存 position
TEST(EnemyTest, savePosition)
{
    Vec2 pos{10, 20};
    Vec2 size{50, 60};
    Enemy enemy(pos, size);
    const Vec2 actual = enemy.position();
    EXPECT_FLOAT_EQ(actual.x, pos.x);
    EXPECT_FLOAT_EQ(actual.y, pos.y);
}
// 2. 构造后能保存 size
TEST(EnemyTest, saveSize)
{
    Vec2 pos{10, 20};
    Vec2 size{50, 60};
    Enemy enemy(pos, size);
    const Vec2 actual = enemy.size();
    EXPECT_FLOAT_EQ(actual.x, size.x);
    EXPECT_FLOAT_EQ(actual.y, size.y);
}
// 3. bounds() 返回的 Rect 与 position/size 一致
TEST(EnemyTest, bounds)
{
    Vec2 pos{10, 20};
    Vec2 size{50, 60};
    Enemy enemy(pos, size);
    const Rect bounds = enemy.bounds();

    EXPECT_FLOAT_EQ(bounds.position.x, pos.x);
    EXPECT_FLOAT_EQ(bounds.position.y, pos.y);
    EXPECT_FLOAT_EQ(bounds.size.x, size.x);
    EXPECT_FLOAT_EQ(bounds.size.y, size.y);
}
