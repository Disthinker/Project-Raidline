#include <gtest/gtest.h>
#include <cmath>
#include "enemy.h"

// 1. 构造后能保存 position
TEST(EnemyTest, savePosition)
{
    Vec2 pos{10, 20};
    Vec2 size{50, 60};
    Vec2 velocity{0.0f, 0.0f};

    Enemy enemy(pos, size, velocity);
    const Vec2 actual = enemy.position();
    EXPECT_FLOAT_EQ(actual.x, pos.x);
    EXPECT_FLOAT_EQ(actual.y, pos.y);
}
// 2. 构造后能保存 size
TEST(EnemyTest, saveSize)
{
    Vec2 pos{10, 20};
    Vec2 size{50, 60};
    Vec2 velocity{0.0f, 0.0f};

    Enemy enemy(pos, size, velocity);
    const Vec2 actual = enemy.size();
    EXPECT_FLOAT_EQ(actual.x, size.x);
    EXPECT_FLOAT_EQ(actual.y, size.y);
}
// 3. bounds() 返回的 Rect 与 position/size 一致
TEST(EnemyTest, bounds)
{
    Vec2 pos{10, 20};
    Vec2 size{50, 60};
    Vec2 velocity{0.0f, 0.0f};

    Enemy enemy(pos, size, velocity);
    const Rect bounds = enemy.bounds();

    EXPECT_FLOAT_EQ(bounds.position.x, pos.x);
    EXPECT_FLOAT_EQ(bounds.position.y, pos.y);
    EXPECT_FLOAT_EQ(bounds.size.x, size.x);
    EXPECT_FLOAT_EQ(bounds.size.y, size.y);
}
// 4. 构造后能保存 velocity
TEST(EnemyTest, saveVelocity)
{
    Vec2 pos{10, 20};
    Vec2 size{50, 60};
    Vec2 velocity{100.0f, 0.0f};

    Enemy enemy(pos, size, velocity);
    const Vec2 savedVelocity = enemy.velocity();

    EXPECT_FLOAT_EQ(savedVelocity.x, velocity.x);
    EXPECT_FLOAT_EQ(savedVelocity.y, velocity.y);
}
// 5. update 后 X 位置会变化
TEST(EnemyTest, xChangesAfterUpdate)
{
    Vec2 pos{10, 20};
    Vec2 size{50, 60};
    Vec2 velocity{100.0f, 0.0f};

    Enemy enemy(pos, size, velocity);
    const Vec2 initialPos = enemy.position();
    enemy.update(0.1f, 1280.0f);
    const Vec2 updatedPos = enemy.position();
    EXPECT_FLOAT_EQ(updatedPos.x, initialPos.x + velocity.x * 0.1f);
}
// 6. update 不改变 Y
TEST(EnemyTest, yDoesNotChangeAfterUpdate)
{
    Vec2 pos{10, 20};
    Vec2 size{50, 60};
    Vec2 velocity{100.0f, 0.0f};

    Enemy enemy(pos, size, velocity);
    const Vec2 initialPos = enemy.position();
    enemy.update(0.1f, 1280.0f);
    const Vec2 updatedPos = enemy.position();
    EXPECT_FLOAT_EQ(updatedPos.y, initialPos.y);
}
// 7. 碰到右边界后反弹
TEST(EnemyTest, rightBoundaryBounce)
{
    Vec2 pos{1200, 20};
    Vec2 size{50, 60};
    Vec2 velocity{2000.0f, 0.0f};

    Enemy enemy(pos, size, velocity);
    const Vec2 initialVelocity = enemy.velocity();
    const Vec2 initialPos = enemy.position();
    enemy.update(0.1f, 1280.0f);
    const Vec2 updatedVelocity = enemy.velocity();
    const Vec2 updatedPos = enemy.position();
    EXPECT_FLOAT_EQ(updatedVelocity.x, -1 * std::abs(initialVelocity.x));
    EXPECT_FLOAT_EQ(updatedVelocity.y, initialVelocity.y);
    EXPECT_FLOAT_EQ(updatedPos.x, 1280.0f - size.x);
    EXPECT_FLOAT_EQ(updatedPos.y, initialPos.y);
}
// 9. 碰到左边界后反弹
TEST(EnemyTest, leftBoundaryBounce)
{
    Vec2 pos{50, 20};
    Vec2 size{50, 60};
    Vec2 velocity{-2000.0f, 0.0f};

    Enemy enemy(pos, size, velocity);
    const Vec2 initialVelocity = enemy.velocity();
    const Vec2 initialPos = enemy.position();
    enemy.update(0.1f, 1280.0f);
    const Vec2 updatedVelocity = enemy.velocity();
    const Vec2 updatedPos = enemy.position();
    EXPECT_FLOAT_EQ(updatedVelocity.x, std::abs(initialVelocity.x));
    EXPECT_FLOAT_EQ(updatedVelocity.y, initialVelocity.y);
    EXPECT_FLOAT_EQ(updatedPos.x, 0.0f);
    EXPECT_FLOAT_EQ(updatedPos.y, initialPos.y);
}