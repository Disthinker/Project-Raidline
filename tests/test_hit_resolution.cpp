#include <gtest/gtest.h>

#include <vector>

#include "hit_resolution.h"

namespace
{
    ShotCollisionCandidate makeShot(
        ShotId shotId,
        Vec2 position,
        Vec2 size = Vec2{10.0F, 10.0F},
        int damage = 1)
    {
        return ShotCollisionCandidate{
            shotId,
            Rect{position, size},
            damage};
    }
}

TEST(HitResolutionTest, LethalHitConsumesShotAndEnemy)
{
    const std::vector<ShotCollisionCandidate> shots{
        makeShot(1, Vec2{10.0F, 10.0F})};
    std::vector<Enemy> enemies{
        Enemy{Vec2{15.0F, 15.0F}, Vec2{10.0F, 10.0F}}};

    const HitResolutionResult result =
        resolveShotEnemyHits(shots, enemies);

    EXPECT_TRUE(enemies.empty());
    ASSERT_EQ(result.hits.size(), 1U);
    ASSERT_EQ(result.consumedShotIds.size(), 1U);
    EXPECT_EQ(result.consumedShotIds[0], 1U);
    EXPECT_EQ(result.enemiesKilled, 1U);
}

TEST(HitResolutionTest, NoHitKeepsEnemiesAndDoesNotConsumeShot)
{
    const std::vector<ShotCollisionCandidate> shots{
        makeShot(1, Vec2{10.0F, 10.0F}),
        makeShot(2, Vec2{20.0F, 20.0F})};
    std::vector<Enemy> enemies{
        Enemy{
            Vec2{100.0F, 100.0F},
            Vec2{10.0F, 10.0F},
            Vec2{},
            3}};

    const HitResolutionResult result =
        resolveShotEnemyHits(shots, enemies);

    ASSERT_EQ(enemies.size(), 1U);
    EXPECT_EQ(enemies[0].health(), 3);
    EXPECT_TRUE(result.hits.empty());
    EXPECT_TRUE(result.consumedShotIds.empty());
    EXPECT_EQ(result.enemiesKilled, 0U);
}

TEST(HitResolutionTest, NonLethalHitProducesDomainResult)
{
    const std::vector<ShotCollisionCandidate> shots{
        makeShot(42, Vec2{20.0F, 30.0F}, Vec2{8.0F, 20.0F}, 2)};
    std::vector<Enemy> enemies{
        Enemy{
            Vec2{24.0F, 35.0F},
            Vec2{20.0F, 20.0F},
            Vec2{},
            3}};

    const HitResolutionResult result =
        resolveShotEnemyHits(shots, enemies);

    ASSERT_EQ(enemies.size(), 1U);
    EXPECT_EQ(enemies[0].health(), 1);
    ASSERT_EQ(result.hits.size(), 1U);
    EXPECT_EQ(result.hits[0].shotId, 42U);
    EXPECT_EQ(result.hits[0].targetKind, HitTargetKind::Enemy);
    EXPECT_FLOAT_EQ(result.hits[0].position.x, 24.0F);
    EXPECT_FLOAT_EQ(result.hits[0].position.y, 40.0F);
    EXPECT_EQ(result.hits[0].damageApplied, 2);
    EXPECT_FALSE(result.hits[0].targetKilled);
    EXPECT_EQ(result.enemiesKilled, 0U);
}

TEST(HitResolutionTest, OneShotHitsAtMostOneEnemy)
{
    const std::vector<ShotCollisionCandidate> shots{
        makeShot(1, Vec2{10.0F, 10.0F}, Vec2{20.0F, 20.0F})};
    std::vector<Enemy> enemies{
        Enemy{Vec2{12.0F, 12.0F}, Vec2{5.0F, 5.0F}},
        Enemy{Vec2{18.0F, 18.0F}, Vec2{5.0F, 5.0F}}};

    const HitResolutionResult result =
        resolveShotEnemyHits(shots, enemies);

    EXPECT_EQ(enemies.size(), 1U);
    EXPECT_EQ(result.hits.size(), 1U);
    EXPECT_EQ(result.enemiesKilled, 1U);
}

TEST(HitResolutionTest, OverkillCountsOnlyOneKill)
{
    const std::vector<ShotCollisionCandidate> shots{
        makeShot(
            1,
            Vec2{10.0F, 10.0F},
            Vec2{10.0F, 10.0F},
            10)};
    std::vector<Enemy> enemies{
        Enemy{
            Vec2{10.0F, 10.0F},
            Vec2{10.0F, 10.0F},
            Vec2{},
            2}};

    const HitResolutionResult result =
        resolveShotEnemyHits(shots, enemies);

    EXPECT_TRUE(enemies.empty());
    ASSERT_EQ(result.hits.size(), 1U);
    EXPECT_TRUE(result.hits[0].targetKilled);
    EXPECT_EQ(result.enemiesKilled, 1U);
}

TEST(HitResolutionTest, MultipleShotsAccumulateDamageAndCountOneKill)
{
    const std::vector<ShotCollisionCandidate> shots{
        makeShot(1, Vec2{10.0F, 15.0F}),
        makeShot(2, Vec2{20.0F, 20.0F})};
    std::vector<Enemy> enemies{
        Enemy{
            Vec2{5.0F, 5.0F},
            Vec2{30.0F, 30.0F},
            Vec2{},
            2}};

    const HitResolutionResult result =
        resolveShotEnemyHits(shots, enemies);

    EXPECT_TRUE(enemies.empty());
    EXPECT_EQ(result.hits.size(), 2U);
    EXPECT_EQ(result.consumedShotIds.size(), 2U);
    EXPECT_EQ(result.enemiesKilled, 1U);
}

TEST(HitResolutionTest, LaterShotCanHitNextEnemyAfterEarlierKill)
{
    const std::vector<ShotCollisionCandidate> shots{
        makeShot(1, Vec2{10.0F, 10.0F}, Vec2{20.0F, 20.0F}),
        makeShot(2, Vec2{10.0F, 10.0F}, Vec2{20.0F, 20.0F})};
    std::vector<Enemy> enemies{
        Enemy{Vec2{12.0F, 12.0F}, Vec2{5.0F, 5.0F}},
        Enemy{Vec2{18.0F, 18.0F}, Vec2{5.0F, 5.0F}}};

    const HitResolutionResult result =
        resolveShotEnemyHits(shots, enemies);

    EXPECT_TRUE(enemies.empty());
    EXPECT_EQ(result.hits.size(), 2U);
    EXPECT_EQ(result.enemiesKilled, 2U);
}

TEST(HitResolutionTest, DeadEnemyDoesNotConsumeLaterShot)
{
    const std::vector<ShotCollisionCandidate> shots{
        makeShot(1, Vec2{10.0F, 10.0F}),
        makeShot(2, Vec2{10.0F, 10.0F})};
    std::vector<Enemy> enemies{
        Enemy{Vec2{10.0F, 10.0F}, Vec2{10.0F, 10.0F}}};

    const HitResolutionResult result =
        resolveShotEnemyHits(shots, enemies);

    EXPECT_TRUE(enemies.empty());
    ASSERT_EQ(result.consumedShotIds.size(), 1U);
    EXPECT_EQ(result.consumedShotIds[0], 1U);
    EXPECT_EQ(result.hits.size(), 1U);
    EXPECT_EQ(result.enemiesKilled, 1U);
}

TEST(HitResolutionTest, InvalidCandidateCannotDamageOrBeConsumed)
{
    const std::vector<ShotCollisionCandidate> shots{
        makeShot(kInvalidShotId, Vec2{10.0F, 10.0F}),
        makeShot(2, Vec2{10.0F, 10.0F}, Vec2{10.0F, 10.0F}, 0)};
    std::vector<Enemy> enemies{
        Enemy{
            Vec2{10.0F, 10.0F},
            Vec2{10.0F, 10.0F},
            Vec2{},
            3}};

    const HitResolutionResult result =
        resolveShotEnemyHits(shots, enemies);

    ASSERT_EQ(enemies.size(), 1U);
    EXPECT_EQ(enemies[0].health(), 3);
    EXPECT_TRUE(result.hits.empty());
    EXPECT_TRUE(result.consumedShotIds.empty());
}
