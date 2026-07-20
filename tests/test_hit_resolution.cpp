#include <gtest/gtest.h>

#include <vector>

#include "hit_resolution.h"

// 默认 Enemy HP 为 1，因此保持原来的一击击杀兼容行为。
TEST(HitResolutionTest, BothDecreaseAfterLethalHit)
{
    std::vector<Projectile> projectiles{
        Projectile{
            Vec2{10.0f, 10.0f},
            Vec2{0.0f, 0.0f},
            10.0f,
            10.0f}};

    std::vector<Enemy> enemies{
        Enemy{
            Vec2{15.0f, 15.0f},
            Vec2{10.0f, 10.0f}}};

    const HitResolutionResult result =
        resolveProjectileEnemyHits(
            projectiles,
            enemies);

    EXPECT_TRUE(projectiles.empty());
    EXPECT_TRUE(enemies.empty());

    EXPECT_EQ(result.hitPositions.size(), 1u);
    EXPECT_EQ(result.enemiesKilled, 1u);
}

TEST(HitResolutionTest, NoHitKeepsBothCollections)
{
    std::vector<Projectile> projectiles{
        Projectile{
            Vec2{10.0f, 10.0f},
            Vec2{},
            10.0f,
            10.0f},
        Projectile{
            Vec2{20.0f, 20.0f},
            Vec2{},
            10.0f,
            10.0f}};

    std::vector<Enemy> enemies{
        Enemy{
            Vec2{100.0f, 100.0f},
            Vec2{10.0f, 10.0f},
            Vec2{},
            3},
        Enemy{
            Vec2{200.0f, 200.0f},
            Vec2{10.0f, 10.0f},
            Vec2{},
            3}};

    const HitResolutionResult result =
        resolveProjectileEnemyHits(
            projectiles,
            enemies);

    EXPECT_EQ(projectiles.size(), 2u);
    ASSERT_EQ(enemies.size(), 2u);

    EXPECT_EQ(enemies[0].health(), 3);
    EXPECT_EQ(enemies[1].health(), 3);

    EXPECT_TRUE(result.hitPositions.empty());
    EXPECT_EQ(result.enemiesKilled, 0u);
}

TEST(
    HitResolutionTest,
    NonLethalHitConsumesProjectileAndKeepsEnemy)
{
    std::vector<Projectile> projectiles{
        Projectile{
            Vec2{10.0f, 10.0f},
            Vec2{},
            10.0f,
            10.0f,
            1}};

    std::vector<Enemy> enemies{
        Enemy{
            Vec2{15.0f, 15.0f},
            Vec2{10.0f, 10.0f},
            Vec2{},
            2}};

    const HitResolutionResult result =
        resolveProjectileEnemyHits(
            projectiles,
            enemies);

    EXPECT_TRUE(projectiles.empty());

    ASSERT_EQ(enemies.size(), 1u);
    EXPECT_EQ(enemies[0].health(), 1);
    EXPECT_FALSE(enemies[0].isDead());

    EXPECT_EQ(result.hitPositions.size(), 1u);
    EXPECT_EQ(result.enemiesKilled, 0u);
}

TEST(
    HitResolutionTest,
    LethalHitRemovesEnemyAndCountsKill)
{
    std::vector<Projectile> projectiles{
        Projectile{
            Vec2{10.0f, 10.0f},
            Vec2{},
            10.0f,
            10.0f,
            2}};

    std::vector<Enemy> enemies{
        Enemy{
            Vec2{15.0f, 15.0f},
            Vec2{10.0f, 10.0f},
            Vec2{},
            2}};

    const HitResolutionResult result =
        resolveProjectileEnemyHits(
            projectiles,
            enemies);

    EXPECT_TRUE(projectiles.empty());
    EXPECT_TRUE(enemies.empty());

    EXPECT_EQ(result.hitPositions.size(), 1u);
    EXPECT_EQ(result.enemiesKilled, 1u);
}

TEST(
    HitResolutionTest,
    OverkillCountsOnlyOneKill)
{
    std::vector<Projectile> projectiles{
        Projectile{
            Vec2{10.0f, 10.0f},
            Vec2{},
            10.0f,
            10.0f,
            10}};

    std::vector<Enemy> enemies{
        Enemy{
            Vec2{15.0f, 15.0f},
            Vec2{10.0f, 10.0f},
            Vec2{},
            2}};

    const HitResolutionResult result =
        resolveProjectileEnemyHits(
            projectiles,
            enemies);

    EXPECT_TRUE(projectiles.empty());
    EXPECT_TRUE(enemies.empty());

    EXPECT_EQ(result.hitPositions.size(), 1u);
    EXPECT_EQ(result.enemiesKilled, 1u);
}

TEST(
    HitResolutionTest,
    OneProjectileHitsAtMostOneEnemy)
{
    std::vector<Projectile> projectiles{
        Projectile{
            Vec2{10.0f, 10.0f},
            Vec2{},
            20.0f,
            20.0f}};

    std::vector<Enemy> enemies{
        Enemy{
            Vec2{12.0f, 12.0f},
            Vec2{5.0f, 5.0f}},
        Enemy{
            Vec2{18.0f, 18.0f},
            Vec2{5.0f, 5.0f}}};

    const HitResolutionResult result =
        resolveProjectileEnemyHits(
            projectiles,
            enemies);

    EXPECT_TRUE(projectiles.empty());

    // Projectile 只击杀第一个碰到的 Enemy。
    EXPECT_EQ(enemies.size(), 1u);

    EXPECT_EQ(result.hitPositions.size(), 1u);
    EXPECT_EQ(result.enemiesKilled, 1u);
}

TEST(
    HitResolutionTest,
    MultipleProjectilesAccumulateDamageOnOneEnemy)
{
    std::vector<Projectile> projectiles{
        Projectile{
            Vec2{10.0f, 15.0f},
            Vec2{},
            10.0f,
            10.0f,
            1},
        Projectile{
            Vec2{20.0f, 20.0f},
            Vec2{},
            10.0f,
            10.0f,
            1}};

    std::vector<Enemy> enemies{
        Enemy{
            Vec2{5.0f, 5.0f},
            Vec2{30.0f, 30.0f},
            Vec2{},
            2}};

    const HitResolutionResult result =
        resolveProjectileEnemyHits(
            projectiles,
            enemies);

    EXPECT_TRUE(projectiles.empty());
    EXPECT_TRUE(enemies.empty());

    // 两次有效命中，各产生一个 impact position。
    EXPECT_EQ(result.hitPositions.size(), 2u);

    // 同一个 Enemy 只发生一次 alive -> dead。
    EXPECT_EQ(result.enemiesKilled, 1u);
}

TEST(
    HitResolutionTest,
    DeadEnemyIsNotDamagedOrCountedAgain)
{
    std::vector<Projectile> projectiles{
        Projectile{
            Vec2{10.0f, 15.0f},
            Vec2{},
            10.0f,
            10.0f,
            1},
        Projectile{
            Vec2{15.0f, 15.0f},
            Vec2{},
            10.0f,
            10.0f,
            1},
        Projectile{
            Vec2{20.0f, 15.0f},
            Vec2{},
            10.0f,
            10.0f,
            1}};

    std::vector<Enemy> enemies{
        Enemy{
            Vec2{5.0f, 5.0f},
            Vec2{30.0f, 30.0f},
            Vec2{},
            2}};

    const HitResolutionResult result =
        resolveProjectileEnemyHits(
            projectiles,
            enemies);

    // 前两枚 Projectile 扣除 2 HP。
    // 第三枚看到 Enemy 已死亡，因此不被消耗。
    ASSERT_EQ(projectiles.size(), 1u);
    EXPECT_TRUE(enemies.empty());

    EXPECT_EQ(result.hitPositions.size(), 2u);
    EXPECT_EQ(result.enemiesKilled, 1u);
}

TEST(
    HitResolutionTest,
    HitPositionEqualsProjectileCenter)
{
    std::vector<Projectile> projectiles{
        Projectile{
            Vec2{20.0f, 30.0f},
            Vec2{},
            8.0f,
            20.0f,
            1}};

    std::vector<Enemy> enemies{
        Enemy{
            Vec2{24.0f, 35.0f},
            Vec2{20.0f, 20.0f},
            Vec2{},
            2}};

    const HitResolutionResult result =
        resolveProjectileEnemyHits(
            projectiles,
            enemies);

    ASSERT_EQ(result.hitPositions.size(), 1u);

    EXPECT_FLOAT_EQ(
        result.hitPositions[0].x,
        24.0f);

    EXPECT_FLOAT_EQ(
        result.hitPositions[0].y,
        40.0f);

    EXPECT_EQ(result.enemiesKilled, 0u);
}

TEST(
    HitResolutionTest,
    ProjectileCanHitNextLivingEnemyAfterEarlierEnemyDies)
{
    std::vector<Projectile> projectiles{
        Projectile{
            Vec2{10.0f, 10.0f},
            Vec2{},
            20.0f,
            20.0f,
            1},
        Projectile{
            Vec2{10.0f, 10.0f},
            Vec2{},
            20.0f,
            20.0f,
            1}};

    std::vector<Enemy> enemies{
        Enemy{
            Vec2{12.0f, 12.0f},
            Vec2{5.0f, 5.0f},
            Vec2{},
            1},
        Enemy{
            Vec2{18.0f, 18.0f},
            Vec2{5.0f, 5.0f},
            Vec2{},
            1}};

    const HitResolutionResult result =
        resolveProjectileEnemyHits(
            projectiles,
            enemies);

    EXPECT_TRUE(projectiles.empty());
    EXPECT_TRUE(enemies.empty());

    EXPECT_EQ(result.hitPositions.size(), 2u);
    EXPECT_EQ(result.enemiesKilled, 2u);
}