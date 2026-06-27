#include <gtest/gtest.h>

#include "gameplay_world.h"

// 初始 Player 位置是 (640, 360)
TEST(gameplayWorldTest, initialPlayerPosition)
{
    GameplayWorld world;
    Vec2 expectedPosition{640.0f, 360.0f};
    EXPECT_EQ(world.player().position(), expectedPosition);
}

// 初始 Projectile 集合为空
TEST(gameplay_WorldTest, initialProjectilesEmpty)
{
    GameplayWorld world;
    EXPECT_TRUE(world.projectiles().empty());
}

// 初始 Enemy 有 1 个，position=(600,100)，size=(50,50)
TEST(gameplay_WorldTest, initialEnemiesNotEmpty)
{
    GameplayWorld world;
    Vec2 expectedPosition{600.0f, 100.0f};
    Vec2 expectedSize{50.0f, 50.0f};
    EXPECT_FALSE(world.enemies().empty());
    EXPECT_EQ(world.enemies()[0].position(), expectedPosition);
    EXPECT_EQ(world.enemies()[0].size(), expectedSize);
}