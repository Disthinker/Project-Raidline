#include <gtest/gtest.h>
#include "projectile.h"

namespace
{
    constexpr float deltaTime{0.1f};
}

// 初始状态能保存
TEST(ProjectileTest, StoresInitialState)
{
    Projectile projectile(
        Vec2{100.0f, 100.0f}, // 位置
        Vec2{0.0f, -100.0f},  // 速度
        4.0f, 12.0f           // 宽高
    );

    EXPECT_FLOAT_EQ(projectile.position().x, 100.0f);
    EXPECT_FLOAT_EQ(projectile.position().y, 100.0f);
    EXPECT_FLOAT_EQ(projectile.width(), 4.0f);
    EXPECT_FLOAT_EQ(projectile.height(), 12.0f);
}

// update 使用 deltaTime 移动
TEST(ProjectileTest, UpdateMovesByDeltaTime)
{
    Projectile moveProjectile(
        Vec2{100.0f, 100.0f},
        Vec2{0.0f, -100.0f},
        4.0f, 12.0f);

    moveProjectile.update(deltaTime);

    EXPECT_FLOAT_EQ(moveProjectile.position().x, 100.0f);
    EXPECT_FLOAT_EQ(moveProjectile.position().y, 90.0f);
}

// 完全飞出上边界后判定 outside
TEST(ProjectileTest, IsOutsideAfterFullyLeavingTopEdge)
{
    Projectile outsideProjectile(
        Vec2{100.0f, -10.0f},
        Vec2{0.0f, -100.0f},
        4.0f, 12.0f);

    outsideProjectile.update(deltaTime);

    EXPECT_TRUE(outsideProjectile.isOutside(1280.0f, 720.0f));
}

// 在世界范围内时不是 outside
TEST(ProjectileTest, IsNotOutsideWhileInsideWorld)
{
    Projectile insideProjectile(
        Vec2{100.0f, 300.0f},
        Vec2{0.0f, -100.0f},
        4.0f, 12.0f);

    insideProjectile.update(deltaTime);

    EXPECT_FALSE(insideProjectile.isOutside(1280.0f, 720.0f));
}

// 边界测试
TEST(ProjectileTest, IsNotOutsideWhilePartiallyVisibleAboveTopEdge)
{
    Projectile projectile(
        Vec2{100.0f, -10.0f},
        Vec2{0.0f, -100.0f},
        4.0f, 12.0f);

    EXPECT_FALSE(projectile.isOutside(1280.0f, 720.0f));
}

// bound测试
TEST(ProjectileTest, BoundsUsesPositionAndSize)
{
    Projectile projectile(
        Vec2{100.0f, 200.0f},
        Vec2{0.0f, -100.0f},
        8.0f,
        20.0f);

    const Rect bounds = projectile.bounds();

    EXPECT_FLOAT_EQ(bounds.position.x, 100.0f);
    EXPECT_FLOAT_EQ(bounds.position.y, 200.0f);
    EXPECT_FLOAT_EQ(bounds.size.x, 8.0f);
    EXPECT_FLOAT_EQ(bounds.size.y, 20.0f);
}

TEST(ProjectileTest, StoresDamage)
{
    const Projectile projectile{
        Vec2{},
        Vec2{100.0f, 0.0f},
        8.0f,
        20.0f,
        2};

    EXPECT_EQ(projectile.damage(), 2);
}

TEST(ProjectileTest, RejectsNonPositiveDamage)
{
    EXPECT_THROW(
        static_cast<void>(
            Projectile{Vec2{}, Vec2{}, 8.0f, 20.0f, 0}),
        std::invalid_argument);

    EXPECT_THROW(
        static_cast<void>(
            Projectile{Vec2{}, Vec2{}, 8.0f, 20.0f, -1}),
        std::invalid_argument);
}