#include <gtest/gtest.h>
#include "enemy.h"

// 1. 构造后能保存 position
TEST(EnemyTest, savePosition)
{
    Vec2 pos{10, 20};
    Vec2 size{50, 60};
    Enemy enemy(pos, size);
    EXPECT_EQ(enemy.position(), pos);
}
// 2. 构造后能保存 size
TEST(EnemyTest, saveSize)
{
    Vec2 pos{10, 20};
    Vec2 size{50, 60};
    Enemy enemy(pos, size);
    EXPECT_EQ(enemy.size(), size);
}
// 3. bounds() 返回的 Rect 与 position/size 一致
TEST(EnemyTest, bounds)
{
    Vec2 pos{10, 20};
    Vec2 size{50, 60};
    Enemy enemy(pos, size);
    Rect expected = {pos, size};
    EXPECT_EQ(enemy.bounds(), expected);
}
