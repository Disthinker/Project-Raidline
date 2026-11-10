#include <gtest/gtest.h>

#include <cmath>
#include <vector>

#include "hit_resolution.h"

namespace
{
    EnemyRoster<> makeEnemies(std::initializer_list<Enemy> values)
    {
        EnemyRoster<> result;
        CombatTargetId id = 1;
        for (auto enemy : values)
        {
            if (enemy.combatTargetId() == kInvalidCombatTargetId)
                enemy = Enemy{enemy.position(), enemy.size(), enemy.velocity(), enemy.maxHealth(), id};
            result.spawn(std::move(enemy));
            ++id;
        }
        return result;
    }

    ShotCollisionCandidate makeShot(
        ShotId shotId,
        Vec2 position,
        Vec2 size = Vec2{10.0F, 10.0F},
        int damage = 1,
        std::optional<ShotAimIntent> aimIntent = std::nullopt)
    {
        const Vec2 center{
            position.x + size.x / 2.0F,
            position.y + size.y / 2.0F};
        return ShotCollisionCandidate{
            shotId,
            center,
            center,
            std::max(size.x, size.y),
            damage,
            aimIntent};
    }
}

TEST(HitResolutionTest, LethalHitConsumesShotAndEnemy)
{
    const std::vector<ShotCollisionCandidate> shots{
        makeShot(1, Vec2{10.0F, 10.0F})};
    auto enemies = makeEnemies({
        Enemy{Vec2{15.0F, 15.0F}, Vec2{10.0F, 10.0F}}});

    const HitResolutionResult result =
        resolveShotEnemyHits(shots, enemies);

    EXPECT_TRUE(enemies.empty());
    ASSERT_EQ(result.hits.size(), 1U);
    ASSERT_EQ(result.consumedShotIds.size(), 1U);
    EXPECT_EQ(result.consumedShotIds[0], 1U);
    EXPECT_EQ(result.enemiesKilled, 1U);
    ASSERT_EQ(result.removals.size(), 1U);
    EXPECT_EQ(result.removals.front().id, 1U);
}

TEST(HitResolutionTest, NoHitKeepsEnemiesAndDoesNotConsumeShot)
{
    const std::vector<ShotCollisionCandidate> shots{
        makeShot(1, Vec2{10.0F, 10.0F}),
        makeShot(2, Vec2{20.0F, 20.0F})};
    auto enemies = makeEnemies({
        Enemy{
            Vec2{100.0F, 100.0F},
            Vec2{10.0F, 10.0F},
            Vec2{},
            3}});

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
    auto enemies = makeEnemies({
        Enemy{
            Vec2{24.0F, 35.0F},
            Vec2{20.0F, 20.0F},
            Vec2{},
            3}});

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
    EXPECT_EQ(result.hits[0].region, HitRegion::Torso);
    EXPECT_EQ(result.hits[0].semantic, HitSemantic::Normal);
    EXPECT_EQ(result.enemiesKilled, 0U);
}

TEST(HitResolutionTest, HeadAndLegHitsAreResolvedByDomainGeometry)
{
    auto headEnemies = makeEnemies({
        Enemy{
            Vec2{100.0F, 100.0F},
            Vec2{40.0F, 80.0F},
            Vec2{},
            10,
            71}});
    const HitResolutionResult head = resolveShotEnemyHits(
        {makeShot(
            7,
            Vec2{110.0F, 102.0F},
            Vec2{8.0F, 8.0F},
            3,
            ShotAimIntent{71, HitRegion::Head, false})},
        headEnemies);
    ASSERT_EQ(head.hits.size(), 1U);
    EXPECT_EQ(head.hits[0].region, HitRegion::Head);
    EXPECT_EQ(head.hits[0].semantic, HitSemantic::Headshot);
    EXPECT_EQ(head.hits[0].damageApplied, 6);

    auto legEnemies = makeEnemies({
        Enemy{Vec2{100.0F, 100.0F}, Vec2{40.0F, 80.0F}, Vec2{}, 10}});
    const HitResolutionResult legs = resolveShotEnemyHits(
        {makeShot(8, Vec2{110.0F, 168.0F}, Vec2{8.0F, 8.0F}, 4)},
        legEnemies);
    ASSERT_EQ(legs.hits.size(), 1U);
    EXPECT_EQ(legs.hits[0].region, HitRegion::Legs);
    EXPECT_EQ(legs.hits[0].semantic, HitSemantic::Normal);
    EXPECT_EQ(legs.hits[0].damageApplied, 3);
}

TEST(HitResolutionTest,
     HeadshotRequiresMatchingFireTimeAimAndPhysicalImpact)
{
    const auto makeEnemy = []
    {
        return Enemy{
            Vec2{100.0F, 100.0F},
            Vec2{40.0F, 80.0F},
            Vec2{},
            20,
            81};
    };

    auto noAimEnemies = makeEnemies({makeEnemy()});
    const HitResolutionResult noAim = resolveShotEnemyHits(
        {ShotCollisionCandidate{
            20,
            Vec2{0.0F, 110.0F},
            Vec2{300.0F, 110.0F},
            2.0F,
            3,
            std::nullopt}},
        noAimEnemies);
    ASSERT_EQ(noAim.hits.size(), 1U);
    EXPECT_EQ(noAim.hits[0].region, HitRegion::Torso);
    EXPECT_EQ(noAim.hits[0].semantic, HitSemantic::Normal);
    EXPECT_EQ(noAim.hits[0].damageApplied, 3);

    auto wrongTargetEnemies = makeEnemies({makeEnemy()});
    const HitResolutionResult wrongTarget = resolveShotEnemyHits(
        {ShotCollisionCandidate{
            21,
            Vec2{0.0F, 110.0F},
            Vec2{300.0F, 110.0F},
            2.0F,
            3,
            ShotAimIntent{82, HitRegion::Head, false}}},
        wrongTargetEnemies);
    ASSERT_EQ(wrongTarget.hits.size(), 1U);
    EXPECT_EQ(wrongTarget.hits[0].semantic, HitSemantic::Normal);

    auto matchedEnemies = makeEnemies({makeEnemy()});
    const HitResolutionResult matched = resolveShotEnemyHits(
        {ShotCollisionCandidate{
            22,
            Vec2{0.0F, 110.0F},
            Vec2{300.0F, 110.0F},
            2.0F,
            3,
            ShotAimIntent{81, HitRegion::Head, false}}},
        matchedEnemies);
    ASSERT_EQ(matched.hits.size(), 1U);
    EXPECT_EQ(matched.hits[0].region, HitRegion::Head);
    EXPECT_EQ(matched.hits[0].semantic, HitSemantic::Headshot);
    EXPECT_EQ(matched.hits[0].damageApplied, 6);
}

TEST(HitResolutionTest,
     WeakPointRequiresMatchingTargetRegionAndPhysicalImpact)
{
    auto enemies = makeEnemies({
        Enemy{
            Vec2{100.0F, 100.0F},
            Vec2{40.0F, 80.0F},
            Vec2{},
            20,
            91}});
    const HitResolutionResult matched = resolveShotEnemyHits(
        {ShotCollisionCandidate{
            23,
            Vec2{0.0F, 140.0F},
            Vec2{300.0F, 140.0F},
            2.0F,
            4,
            ShotAimIntent{91, HitRegion::Torso, true}}},
        enemies);

    ASSERT_EQ(matched.hits.size(), 1U);
    EXPECT_EQ(matched.hits[0].semantic, HitSemantic::WeakPoint);
    EXPECT_EQ(matched.hits[0].damageApplied, 6);

    auto mismatchEnemies = makeEnemies({
        Enemy{
            Vec2{100.0F, 100.0F},
            Vec2{40.0F, 80.0F},
            Vec2{},
            20,
            91}});
    const HitResolutionResult mismatch = resolveShotEnemyHits(
        {ShotCollisionCandidate{
            24,
            Vec2{0.0F, 140.0F},
            Vec2{300.0F, 140.0F},
            2.0F,
            4,
            ShotAimIntent{91, HitRegion::Head, true}}},
        mismatchEnemies);
    ASSERT_EQ(mismatch.hits.size(), 1U);
    EXPECT_EQ(mismatch.hits[0].semantic, HitSemantic::Normal);
    EXPECT_EQ(mismatch.hits[0].damageApplied, 4);
}

TEST(HitResolutionTest, AimRegionQueryRejectsPointsBeforeOrBehindTarget)
{
    const Rect enemyBounds{
        Vec2{100.0F, 100.0F},
        Vec2{40.0F, 80.0F}};

    EXPECT_FALSE(hitRegionAtPoint(enemyBounds, Vec2{90.0F, 110.0F}));
    EXPECT_FALSE(hitRegionAtPoint(enemyBounds, Vec2{150.0F, 110.0F}));
    EXPECT_EQ(
        hitRegionAtPoint(enemyBounds, Vec2{120.0F, 110.0F}),
        std::optional<HitRegion>{HitRegion::Head});
}

TEST(HitResolutionTest, OneShotHitsAtMostOneEnemy)
{
    const std::vector<ShotCollisionCandidate> shots{
        makeShot(1, Vec2{10.0F, 10.0F}, Vec2{20.0F, 20.0F})};
    auto enemies = makeEnemies({
        Enemy{Vec2{12.0F, 12.0F}, Vec2{5.0F, 5.0F}},
        Enemy{Vec2{18.0F, 18.0F}, Vec2{5.0F, 5.0F}}});

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
    auto enemies = makeEnemies({
        Enemy{
            Vec2{10.0F, 10.0F},
            Vec2{10.0F, 10.0F},
            Vec2{},
            2}});

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
    auto enemies = makeEnemies({
        Enemy{
            Vec2{5.0F, 5.0F},
            Vec2{30.0F, 30.0F},
            Vec2{},
            2}});

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
    auto enemies = makeEnemies({
        Enemy{Vec2{12.0F, 12.0F}, Vec2{5.0F, 5.0F}},
        Enemy{Vec2{18.0F, 18.0F}, Vec2{5.0F, 5.0F}}});

    const HitResolutionResult result =
        resolveShotEnemyHits(shots, enemies);

    EXPECT_TRUE(enemies.empty());
    EXPECT_EQ(result.hits.size(), 2U);
    EXPECT_EQ(result.enemiesKilled, 2U);
    ASSERT_EQ(result.removals.size(), 2U);
    EXPECT_EQ(result.removals[0].id, 1U);
    EXPECT_EQ(result.removals[1].id, 2U);
}

TEST(HitResolutionTest, DeadEnemyDoesNotConsumeLaterShot)
{
    const std::vector<ShotCollisionCandidate> shots{
        makeShot(1, Vec2{10.0F, 10.0F}),
        makeShot(2, Vec2{10.0F, 10.0F})};
    auto enemies = makeEnemies({
        Enemy{Vec2{10.0F, 10.0F}, Vec2{10.0F, 10.0F}}});

    const HitResolutionResult result =
        resolveShotEnemyHits(shots, enemies);

    EXPECT_TRUE(enemies.empty());
    ASSERT_EQ(result.consumedShotIds.size(), 1U);
    EXPECT_EQ(result.consumedShotIds[0], 1U);
    EXPECT_EQ(result.hits.size(), 1U);
    EXPECT_EQ(result.enemiesKilled, 1U);
}

TEST(HitResolutionTest, PreDeadEnemyReportsStableIdentity)
{
    auto enemies = makeEnemies({
        Enemy{Vec2{10.0F, 10.0F}, Vec2{10.0F, 10.0F}},
        Enemy{Vec2{30.0F, 10.0F}, Vec2{10.0F, 10.0F}}});
    ASSERT_TRUE(enemies.front().takeDamage(enemies.front().maxHealth()));

    const HitResolutionResult result = resolveShotEnemyHits({}, enemies);

    ASSERT_EQ(enemies.size(), 1U);
    EXPECT_FLOAT_EQ(enemies.front().position().x, 30.0F);
    EXPECT_EQ(result.enemiesKilled, 0U);
    ASSERT_EQ(result.removals.size(), 1U);
    EXPECT_EQ(result.removals[0].id, 1U);
}

TEST(HitResolutionTest, InvalidCandidateCannotDamageOrBeConsumed)
{
    const std::vector<ShotCollisionCandidate> shots{
        makeShot(kInvalidShotId, Vec2{10.0F, 10.0F}),
        makeShot(2, Vec2{10.0F, 10.0F}, Vec2{10.0F, 10.0F}, 0)};
    auto enemies = makeEnemies({
        Enemy{
            Vec2{10.0F, 10.0F},
            Vec2{10.0F, 10.0F},
            Vec2{},
            3}});

    const HitResolutionResult result =
        resolveShotEnemyHits(shots, enemies);

    ASSERT_EQ(enemies.size(), 1U);
    EXPECT_EQ(enemies[0].health(), 3);
    EXPECT_TRUE(result.hits.empty());
    EXPECT_TRUE(result.consumedShotIds.empty());
}

TEST(HitResolutionTest, ContinuousSweepHitsThinTargetWithoutTunnelling)
{
    const std::vector<ShotCollisionCandidate> shots{
        ShotCollisionCandidate{
            9,
            Vec2{0.0F, 50.0F},
            Vec2{300.0F, 50.0F},
            2.0F,
            1}};
    auto enemies = makeEnemies({
        Enemy{Vec2{149.0F, 45.0F}, Vec2{2.0F, 10.0F}}});

    const HitResolutionResult result =
        resolveShotEnemyHits(shots, enemies);

    EXPECT_TRUE(enemies.empty());
    ASSERT_EQ(result.hits.size(), 1U);
    EXPECT_EQ(result.hits[0].shotId, 9U);
    EXPECT_NEAR(result.hits[0].position.x, 149.0F, 0.001F);
}

TEST(HitResolutionTest, ContinuousSweepChoosesNearestTarget)
{
    const std::vector<ShotCollisionCandidate> shots{
        ShotCollisionCandidate{
            10,
            Vec2{0.0F, 50.0F},
            Vec2{300.0F, 50.0F},
            2.0F,
            1}};
    auto enemies = makeEnemies({
        Enemy{Vec2{200.0F, 45.0F}, Vec2{10.0F, 10.0F}},
        Enemy{Vec2{100.0F, 45.0F}, Vec2{10.0F, 10.0F}}});

    const HitResolutionResult result =
        resolveShotEnemyHits(shots, enemies);

    ASSERT_EQ(enemies.size(), 1U);
    EXPECT_FLOAT_EQ(enemies[0].position().x, 200.0F);
    ASSERT_EQ(result.hits.size(), 1U);
    EXPECT_NEAR(result.hits[0].position.x, 100.0F, 0.001F);
}

TEST(HitResolutionTest, NearestObstacleConsumesShotBeforeEnemy)
{
    const std::vector<ShotCollisionCandidate> shots{
        ShotCollisionCandidate{
            11,
            Vec2{0.0F, 50.0F},
            Vec2{300.0F, 50.0F},
            2.0F,
            4}};
    auto enemies = makeEnemies({
        Enemy{Vec2{200.0F, 40.0F}, Vec2{20.0F, 20.0F}, Vec2{}, 5}});
    const std::vector<BallisticBlocker> blockers{
        BallisticBlocker{1, Rect{Vec2{100.0F, 30.0F}, Vec2{20.0F, 40.0F}}}};

    const HitResolutionResult result = resolveShotHits(
        shots,
        enemies,
        blockers);

    ASSERT_EQ(enemies.size(), 1U);
    EXPECT_EQ(enemies.front().health(), 5);
    ASSERT_EQ(result.hits.size(), 1U);
    EXPECT_EQ(result.hits.front().targetKind, HitTargetKind::Obstacle);
    EXPECT_EQ(result.hits.front().damageApplied, 0);
    ASSERT_EQ(result.consumedShotIds.size(), 1U);
    EXPECT_EQ(result.consumedShotIds.front(), 11U);
    EXPECT_NEAR(result.hits.front().position.x, 99.0F, 0.001F);
}

TEST(HitResolutionTest, NearestEnemyStillWinsWhenObstacleIsBehind)
{
    const std::vector<ShotCollisionCandidate> shots{
        ShotCollisionCandidate{
            12,
            Vec2{0.0F, 50.0F},
            Vec2{300.0F, 50.0F},
            2.0F,
            2}};
    auto enemies = makeEnemies({
        Enemy{Vec2{80.0F, 40.0F}, Vec2{20.0F, 20.0F}, Vec2{}, 5}});
    const std::vector<BallisticBlocker> blockers{
        BallisticBlocker{1, Rect{Vec2{180.0F, 30.0F}, Vec2{20.0F, 40.0F}}}};

    const HitResolutionResult result = resolveShotHits(
        shots,
        enemies,
        blockers);

    ASSERT_EQ(result.hits.size(), 1U);
    EXPECT_EQ(result.hits.front().targetKind, HitTargetKind::Enemy);
    ASSERT_EQ(enemies.size(), 1U);
    EXPECT_EQ(enemies.front().health(), 3);
}

TEST(HitResolutionTest, ExpansionUsesMuzzleToPhysicalContactRange)
{
    const Enemy infected{{100.0F, 100.0F}, {20.0F, 40.0F}, {}, 12, 1};
    auto enemies = makeEnemies({infected});
    ShotCollisionCandidate shot{
        1, {94.0F, 120.0F}, {108.0F, 120.0F}, 8.0F, 5, std::nullopt,
        2, {0.0F, 120.0F}, 1, 100.0F};

    const HitResolutionResult onBoundary = resolveShotEnemyHits({shot}, enemies);
    ASSERT_EQ(onBoundary.hits.size(), 1U);
    EXPECT_FLOAT_EQ(onBoundary.hits.front().position.x, 100.0F);
    EXPECT_EQ(onBoundary.hits.front().damageApplied, 6);
    ASSERT_EQ(enemies.size(), 1U);
    EXPECT_EQ(enemies.front().health(), 6);

    shot.shotId = 2;
    shot.unarmoredDamageRange = std::nextafter(100.0F, 0.0F);
    const HitResolutionResult beyondBoundary = resolveShotEnemyHits({shot}, enemies);
    ASSERT_EQ(beyondBoundary.hits.size(), 1U);
    EXPECT_EQ(beyondBoundary.hits.front().damageApplied, 5);
    ASSERT_EQ(enemies.size(), 1U);
    EXPECT_EQ(enemies.front().health(), 1);
}

TEST(HitResolutionTest, ExpansionRespectsPhysicalArmorCoverageAndOriginalArmorDamage)
{
    const Enemy protectedInfected{
        {100.0F, 100.0F}, {20.0F, 40.0F}, {}, 20, 1, EnemyArmorState{7, 50}};
    auto enemies = makeEnemies({protectedInfected});
    ShotCollisionCandidate shot{
        1, {94.0F, 120.0F}, {108.0F, 120.0F}, 8.0F, 5, std::nullopt,
        2, {0.0F, 120.0F}, 1, 100.0F};

    const HitResolutionResult blocked = resolveShotEnemyHits({shot}, enemies);
    ASSERT_EQ(blocked.hits.size(), 1U);
    EXPECT_EQ(blocked.hits.front().damageApplied, 2);
    EXPECT_TRUE(blocked.hits.front().armorReducedDamage);
    ASSERT_EQ(enemies.size(), 1U);
    ASSERT_TRUE(enemies.front().torsoArmor());
    EXPECT_EQ(enemies.front().torsoArmor()->durability, 45U);

    shot.shotId = 2;
    shot.penetration = 7;
    const HitResolutionResult penetrated = resolveShotEnemyHits({shot}, enemies);
    ASSERT_EQ(penetrated.hits.size(), 1U);
    EXPECT_EQ(penetrated.hits.front().damageApplied, 5);
    EXPECT_EQ(enemies.front().torsoArmor()->durability, 45U);

    shot.shotId = 3;
    shot.penetration = 2;
    shot.start.y = shot.end.y = shot.shotOrigin.y = 105.0F;
    const HitResolutionResult uncovered = resolveShotEnemyHits({shot}, enemies);
    ASSERT_EQ(uncovered.hits.size(), 1U);
    EXPECT_EQ(uncovered.hits.front().damageApplied, 6);
    EXPECT_EQ(uncovered.hits.front().semantic, HitSemantic::Normal);
    EXPECT_EQ(uncovered.hits.front().region, HitRegion::Torso);
    EXPECT_EQ(enemies.front().torsoArmor()->durability, 45U);

    enemies.front().applyArmorLoss(45);
    shot.shotId = 4;
    shot.start.y = shot.end.y = shot.shotOrigin.y = 120.0F;
    const HitResolutionResult broken = resolveShotEnemyHits({shot}, enemies);
    ASSERT_EQ(broken.hits.size(), 1U);
    EXPECT_EQ(broken.hits.front().damageApplied, 6);
    EXPECT_FALSE(broken.hits.front().armorReducedDamage);
}
