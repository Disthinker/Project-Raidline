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

namespace
{
    constexpr float kEnemyAnimationTestWorldWidth{1280.0f};

    // Enemy 移动动画每帧持续 0.125 秒。
    constexpr float kEnemyMoveFrameDuration{0.125f};
    constexpr float kHalfEnemyMoveFrameDuration{0.0625f};
}

// 正水平速度应让 Enemy 初始朝右。
TEST(EnemyTest, PositiveVelocityInitiallyFacesRight)
{
    Enemy enemy(
        Vec2{100.0f, 20.0f},
        Vec2{50.0f, 60.0f},
        Vec2{100.0f, 0.0f});

    EXPECT_EQ(
        enemy.facingDirection(),
        EnemyFacingDirection::Right);
    EXPECT_TRUE(enemy.isMoving());
    EXPECT_EQ(enemy.currentAnimationFrameIndex(), 0u);
}

// 负水平速度应让 Enemy 初始朝左。
TEST(EnemyTest, NegativeVelocityInitiallyFacesLeft)
{
    Enemy enemy(
        Vec2{100.0f, 20.0f},
        Vec2{50.0f, 60.0f},
        Vec2{-100.0f, 0.0f});

    EXPECT_EQ(
        enemy.facingDirection(),
        EnemyFacingDirection::Left);
    EXPECT_TRUE(enemy.isMoving());
    EXPECT_EQ(enemy.currentAnimationFrameIndex(), 0u);
}

// 零水平速度的 Enemy 不应推进移动动画。
// 当前确定性默认方向为 Right。
TEST(EnemyTest, StationaryEnemyDoesNotAdvanceMovementAnimation)
{
    Enemy enemy(
        Vec2{100.0f, 20.0f},
        Vec2{50.0f, 60.0f},
        Vec2{0.0f, 0.0f});

    enemy.update(
        10.0f,
        kEnemyAnimationTestWorldWidth);

    EXPECT_FALSE(enemy.isMoving());
    EXPECT_EQ(
        enemy.facingDirection(),
        EnemyFacingDirection::Right);
    EXPECT_EQ(enemy.currentAnimationFrameIndex(), 0u);
}

// 多次 update 的时间应在 Enemy Animator 中持续累积。
TEST(EnemyTest, MovementTimeAccumulatesAndAdvancesAnimation)
{
    Enemy enemy(
        Vec2{100.0f, 20.0f},
        Vec2{50.0f, 60.0f},
        Vec2{10.0f, 0.0f});

    enemy.update(
        kHalfEnemyMoveFrameDuration,
        kEnemyAnimationTestWorldWidth);

    ASSERT_TRUE(enemy.isMoving());
    EXPECT_EQ(enemy.currentAnimationFrameIndex(), 0u);

    enemy.update(
        kHalfEnemyMoveFrameDuration,
        kEnemyAnimationTestWorldWidth);

    EXPECT_EQ(enemy.currentAnimationFrameIndex(), 1u);
}

// 撞到右边界后，速度和视觉方向都应切换为向左。
TEST(EnemyTest, RightBoundaryBounceChangesFacingToLeft)
{
    Enemy enemy(
        Vec2{1229.0f, 20.0f},
        Vec2{50.0f, 60.0f},
        Vec2{100.0f, 0.0f});

    enemy.update(
        0.02f,
        kEnemyAnimationTestWorldWidth);

    EXPECT_FLOAT_EQ(enemy.position().x, 1230.0f);
    EXPECT_LT(enemy.velocity().x, 0.0f);
    EXPECT_EQ(
        enemy.facingDirection(),
        EnemyFacingDirection::Left);
}

// 撞到左边界后，速度和视觉方向都应切换为向右。
TEST(EnemyTest, LeftBoundaryBounceChangesFacingToRight)
{
    Enemy enemy(
        Vec2{1.0f, 20.0f},
        Vec2{50.0f, 60.0f},
        Vec2{-100.0f, 0.0f});

    enemy.update(
        0.02f,
        kEnemyAnimationTestWorldWidth);

    EXPECT_FLOAT_EQ(enemy.position().x, 0.0f);
    EXPECT_GT(enemy.velocity().x, 0.0f);
    EXPECT_EQ(
        enemy.facingDirection(),
        EnemyFacingDirection::Right);
}

// 反弹只改变方向，不应重置当前动画步态。
TEST(EnemyTest, BouncePreservesAnimationProgress)
{
    Enemy enemy(
        Vec2{1220.0f, 20.0f},
        Vec2{50.0f, 60.0f},
        Vec2{100.0f, 0.0f});

    // 0.1875 = 1.5 个动画帧。
    // 本次更新同时发生右边界反弹。
    enemy.update(
        0.1875f,
        kEnemyAnimationTestWorldWidth);

    ASSERT_EQ(
        enemy.facingDirection(),
        EnemyFacingDirection::Left);
    ASSERT_EQ(enemy.currentAnimationFrameIndex(), 1u);

    // 上一次保留了半帧时间；
    // 再增加半帧，应进入 frame 2。
    enemy.update(
        kHalfEnemyMoveFrameDuration,
        kEnemyAnimationTestWorldWidth);

    EXPECT_EQ(
        enemy.facingDirection(),
        EnemyFacingDirection::Left);
    EXPECT_EQ(enemy.currentAnimationFrameIndex(), 2u);
}

// 6 帧 Enemy 移动动画应能完整循环回 frame 0。
TEST(EnemyTest, MovementAnimationLoopsAcrossSixFrames)
{
    Enemy enemy(
        Vec2{100.0f, 20.0f},
        Vec2{50.0f, 60.0f},
        Vec2{10.0f, 0.0f});

    // 6 × 0.125 = 0.75 秒。
    enemy.update(
        0.75f,
        kEnemyAnimationTestWorldWidth);

    EXPECT_TRUE(enemy.isMoving());
    EXPECT_EQ(enemy.currentAnimationFrameIndex(), 0u);
}

TEST(EnemyTest, StoresInitialHealth)
{
    Enemy enemy{
        Vec2{10.0f, 20.0f},
        Vec2{50.0f, 60.0f},
        Vec2{},
        3};

    EXPECT_EQ(enemy.health(), 3);
    EXPECT_EQ(enemy.maxHealth(), 3);
    EXPECT_FALSE(enemy.isDead());
}

TEST(EnemyTest, NonLethalDamageKeepsEnemyAlive)
{
    Enemy enemy{
        Vec2{},
        Vec2{50.0f, 50.0f},
        Vec2{},
        3};

    const bool killed = enemy.takeDamage(1);

    EXPECT_FALSE(killed);
    EXPECT_EQ(enemy.health(), 2);
    EXPECT_FALSE(enemy.isDead());
}

TEST(EnemyTest, LethalDamageReportsDeathTransition)
{
    Enemy enemy{
        Vec2{},
        Vec2{50.0f, 50.0f},
        Vec2{},
        3};

    EXPECT_TRUE(enemy.takeDamage(3));
    EXPECT_TRUE(enemy.isDead());
    EXPECT_EQ(enemy.health(), 0);

    EXPECT_FALSE(enemy.takeDamage(1));
}