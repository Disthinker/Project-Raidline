#include <gtest/gtest.h>
#include "hit_resolution.h"

// Projectile 命中 Enemy 后，两个集合数量都减少
TEST(HitResolutionTest, bothDecreaseAfterHit)
{
    std::vector<Projectile> projectiles{
        Projectile(Vec2{10.0f, 10.0f}, Vec2{0.0f, 0.0f}, 10.0f, 10.0f)};

    std::vector<Enemy> enemies{
        Enemy(Vec2{15.0f, 15.0f}, Vec2{10.0f, 10.0f})};

    resolveProjectileEnemyHits(projectiles, enemies);

    EXPECT_EQ(projectiles.size(), 0u);
    EXPECT_EQ(enemies.size(), 0u);
}

// Projectile 未命中 Enemy 时，两个集合都保留
TEST(HitResolutionTest, bothKeepAfterNoHit)
{
    std::vector<Projectile> projectiles{
        Projectile(Vec2{10.0f, 10.0f}, Vec2{0.0f, 0.0f}, 10.0f, 10.0f),
        Projectile(Vec2{20.0f, 20.0f}, Vec2{0.0f, 0.0f}, 10.0f, 10.0f)};

    std::vector<Enemy> enemies{
        Enemy(Vec2{100.0f, 100.0f}, Vec2{10.0f, 10.0f}),
        Enemy(Vec2{30.0f, 30.0f}, Vec2{10.0f, 10.0f})};

    resolveProjectileEnemyHits(projectiles, enemies);

    EXPECT_EQ(projectiles.size(), 2u);
    EXPECT_EQ(enemies.size(), 2u);
}

// 一枚 Projectile 与两个 Enemy 重叠时，只移除一个 Enemy
TEST(HitResolutionTest, OneProjectileHitsTwoEnemies)
{
    std::vector<Projectile> projectiles{
        Projectile(Vec2{10.0f, 10.0f}, Vec2{0.0f, 0.0f}, 20.0f, 20.0f)};

    std::vector<Enemy> enemies{
        Enemy(Vec2{12.0f, 12.0f}, Vec2{5.0f, 5.0f}),
        Enemy(Vec2{18.0f, 18.0f}, Vec2{5.0f, 5.0f})};

    resolveProjectileEnemyHits(projectiles, enemies);

    EXPECT_EQ(projectiles.size(), 0u);
    EXPECT_EQ(enemies.size(), 1u);
}

// 两枚 Projectile 同时重叠同一个 Enemy 时，只移除一个 Enemy，并且不要重复处理同一个 Enemy
TEST(HitResolutionTest, TwoProjectilesHitOneEnemy)
{
    std::vector<Projectile> projectiles{
        Projectile(Vec2{10.0f, 15.0f}, Vec2{0.0f, 0.0f}, 10.0f, 10.0f),
        Projectile(Vec2{20.0f, 30.0f}, Vec2{0.0f, 0.0f}, 10.0f, 10.0f)};

    std::vector<Enemy> enemies{
        Enemy(Vec2{5.0f, 5.0f}, Vec2{30.0f, 30.0f})};

    resolveProjectileEnemyHits(projectiles, enemies);

    EXPECT_EQ(enemies.size(), 0u);
    EXPECT_EQ(projectiles.size(), 1u);
}
