#include <gtest/gtest.h>

#include "collision.h"
#include "gameplay_world.h"

#include <cmath>

namespace
{
    constexpr Vec2 kPlayerStart{100.0F, 200.0F};
    constexpr Vec2 kCompanionStart{172.0F, 216.0F};
    constexpr float kFrame{1.0F / 60.0F};

    RaidWorldConfig supportWorld()
    {
        RaidWorldConfig config;
        config.worldSize = {960.0F, 560.0F};
        config.playerSpawn = kPlayerStart;
        config.extractionPoint = {{880.0F, 470.0F}, {70.0F, 70.0F}};
        config.deferPlayerDamageResolution = true;
        config.fireSupport = FireSupportConfig{20, 7000.0F, 12, 115.0F};
        return config;
    }

    Vec2 center(Rect bounds)
    {
        return {bounds.position.x + bounds.size.x * 0.5F,
                bounds.position.y + bounds.size.y * 0.5F};
    }

    void expectSamePosition(Vec2 actual, Vec2 expected)
    {
        EXPECT_FLOAT_EQ(actual.x, expected.x);
        EXPECT_FLOAT_EQ(actual.y, expected.y);
    }

    void placePlayer(GameplayWorld &world, Vec2 topLeft)
    {
        // Stage adversarial poses while leaving the subsequent world step real.
        ASSERT_TRUE(const_cast<Player &>(world.player()).setPosition(topLeft));
    }
}

TEST(RaidFireSupportRuntimeTest, FollowUsesCollisionAndHoldKeepsTheOutdoorPost)
{
    auto config = supportWorld();
    config.ballisticBlockers = {
        {1, {{240.0F, 0.0F}, {20.0F, 175.0F}}},
        {2, {{240.0F, 257.0F}, {20.0F, 303.0F}}},
    };
    GameplayWorld world{config};
    const auto start = world.fireSupport();
    ASSERT_TRUE(start);
    EXPECT_TRUE(start->outdoor);
    EXPECT_TRUE(start->visible);
    EXPECT_TRUE(start->inInteractionRange);
    EXPECT_EQ(start->health, kFireSupportMaxHealth);
    EXPECT_EQ(start->movementOrder, FireSupportMovementOrder::Follow);
    EXPECT_EQ(start->fireOrder, FireSupportFireOrder::HoldFire);
    expectSamePosition(center(start->bounds), kCompanionStart);

    ASSERT_TRUE(world.setFireSupportOrders(
        FireSupportMovementOrder::Hold, FireSupportFireOrder::HoldFire));
    GameplayInput moveRight;
    moveRight.moveRight = true;
    for (int tick{}; tick < 60; ++tick)
        world.update(moveRight, kFrame);
    GameplayInput moveDown;
    moveDown.moveDown = true;
    for (int tick{}; tick < 32; ++tick)
        world.update(moveDown, kFrame);
    expectSamePosition(center(world.fireSupport()->bounds), kCompanionStart);
    EXPECT_FALSE(world.canInteractWithFireSupport());

    ASSERT_TRUE(world.setFireSupportOrders(
        FireSupportMovementOrder::Follow, FireSupportFireOrder::HoldFire));
    for (int tick{}; tick < 100; ++tick)
    {
        world.update({}, kFrame);
        const Rect bounds = world.fireSupport()->bounds;
        EXPECT_FALSE(isCollision(bounds, config.ballisticBlockers[0].bounds));
        EXPECT_FALSE(isCollision(bounds, config.ballisticBlockers[1].bounds));
    }
    EXPECT_GT(world.fireSupport()->bounds.position.x, 260.0F);
    EXPECT_TRUE(world.canInteractWithFireSupport());

    ASSERT_TRUE(world.setFireSupportOrders(
        FireSupportMovementOrder::Hold, FireSupportFireOrder::HoldFire));
    placePlayer(world, {885.0F, 475.0F});
    world.update({}, 3.1F);
    EXPECT_EQ(world.raidSession().state(), RaidSessionState::Extracted);
    EXPECT_EQ(world.fireSupportHealth(), kFireSupportMaxHealth);
}

TEST(RaidFireSupportRuntimeTest, SupportShotUsesCanonicalHitAndIndependentNoise)
{
    auto config = supportWorld();
    config.ballisticBlockers = {{1, {{100.0F, 254.0F}, {260.0F, 10.0F}}}};
    EnemySpawn target{{243.0F, 206.0F}, {20.0F, 20.0F}, 5};
    target.sourceEnemyId = "support-shot-target";
    config.initialEnemies = {target, EnemySpawn{{232.0F, 275.0F}, {20.0F, 20.0F}, 100}};
    GameplayWorld world{config};
    world.update({}, kFrame);
    ASSERT_FALSE(world.fireSupportShotLastUpdate());
    ASSERT_EQ(world.enemies().size(), 2U);
    EXPECT_FALSE(world.enemies()[1].lastKnownTargetPosition());

    ASSERT_TRUE(world.setFireSupportOrders(
        FireSupportMovementOrder::Hold, FireSupportFireOrder::FreeFire));
    world.update({}, kFrame);
    const auto shot = world.fireSupportShotLastUpdate();
    ASSERT_TRUE(shot);
    expectSamePosition(shot->origin, kCompanionStart);
    EXPECT_FLOAT_EQ(shot->noiseRadius, 115.0F);
    EXPECT_FALSE(world.shotFiredLastUpdate());
    EXPECT_TRUE(world.logicalBallistics().empty());
    EXPECT_TRUE(world.shotFeedbackPresentationSnapshots().empty());
    EXPECT_TRUE(world.takePlayerDamageObservations().empty());
    ASSERT_EQ(world.enemies().size(), 1U);
    EXPECT_EQ(world.score(), 100);
    ASSERT_EQ(world.pendingCorpseDeaths().size(), 1U);
    EXPECT_EQ(world.pendingCorpseDeaths().front().sourceEnemyId, target.sourceEnemyId);
    const auto heard = world.enemies().front().lastKnownTargetPosition();
    ASSERT_TRUE(heard);
    expectSamePosition(*heard, kCompanionStart);
}

TEST(RaidFireSupportRuntimeTest, SuppressesFireAcrossWallAndAcrossPlayer)
{
    auto config = supportWorld();
    config.initialEnemies = {EnemySpawn{{329.0F, 206.0F}, {20.0F, 20.0F}, 100}};
    config.ballisticBlockers = {{1, {{235.0F, 110.0F}, {14.0F, 190.0F}}}};
    GameplayWorld wall{config};
    ASSERT_TRUE(wall.setFireSupportOrders(
        FireSupportMovementOrder::Hold, FireSupportFireOrder::FreeFire));
    wall.update({}, kFrame);
    EXPECT_FALSE(wall.fireSupportShotLastUpdate());

    config.ballisticBlockers.clear();
    GameplayWorld protectedLane{config};
    ASSERT_TRUE(protectedLane.setFireSupportOrders(
        FireSupportMovementOrder::Hold, FireSupportFireOrder::FreeFire));
    placePlayer(protectedLane, {204.0F, 200.0F});
    protectedLane.update({}, kFrame);
    EXPECT_FALSE(protectedLane.fireSupportShotLastUpdate());
    ASSERT_EQ(protectedLane.enemies().size(), 1U);
    EXPECT_EQ(protectedLane.enemies().front().health(), 100);
}

TEST(RaidFireSupportRuntimeTest, CadenceAndInFlightSafetyUseIndependentShooterState)
{
    auto config = supportWorld();
    config.fireSupport->projectileSpeed = 400.0F;
    config.initialEnemies = {EnemySpawn{{313.0F, 206.0F}, {20.0F, 20.0F}, 100}};
    GameplayWorld world{config};
    ASSERT_TRUE(world.setFireSupportOrders(
        FireSupportMovementOrder::Hold, FireSupportFireOrder::FreeFire));
    world.update({}, kFrame);
    ASSERT_TRUE(world.fireSupportShotLastUpdate());
    EXPECT_EQ(world.enemies().front().health(), 100);

    placePlayer(world, {194.0F, 200.0F});
    world.update({}, 0.3F);
    EXPECT_FALSE(world.fireSupportShotLastUpdate());
    EXPECT_EQ(world.enemies().front().health(), 100);
    EXPECT_TRUE(world.takePlayerDamageObservations().empty());

    placePlayer(world, kPlayerStart);
    world.update({}, 0.8F);
    EXPECT_FALSE(world.fireSupportShotLastUpdate());
    world.update({}, 0.12F);
    EXPECT_TRUE(world.fireSupportShotLastUpdate());
    EXPECT_FALSE(world.shotFiredLastUpdate());
}

TEST(RaidFireSupportRuntimeTest, EnemyWindupKeepsCompanionTargetWhenPlayerMovesCloser)
{
    auto config = supportWorld();
    config.initialEnemies = {EnemySpawn{{215.0F, 197.0F}, {38.0F, 38.0F}, 100}};
    GameplayWorld world{config};
    ASSERT_TRUE(world.setFireSupportOrders(
        FireSupportMovementOrder::Hold, FireSupportFireOrder::HoldFire));
    for (int tick{}; tick < 120 && world.enemies().front().attackPhase() == EnemyAttackPhase::Idle; ++tick)
        world.update({}, kFrame);
    ASSERT_EQ(world.enemies().front().attackPhase(), EnemyAttackPhase::Windup);

    placePlayer(world, {188.0F, 200.0F});
    for (int tick{}; tick < 40 && world.fireSupportHealth() == kFireSupportMaxHealth; ++tick)
        world.update({}, kFrame);
    ASSERT_TRUE(world.fireSupportHealth());
    EXPECT_LT(*world.fireSupportHealth(), kFireSupportMaxHealth);
    EXPECT_FALSE(world.player().isControlled());
    EXPECT_TRUE(world.takePlayerDamageObservations().empty());
}

TEST(RaidFireSupportRuntimeTest, DeathKeepsOneReachableOutdoorPositionAcrossPortals)
{
    auto config = supportWorld();
    config.initialEnemies = {EnemySpawn{{215.0F, 197.0F}, {38.0F, 38.0F}, 100}};
    RaidInteriorWorldConfig interior;
    interior.id = RaidSpaceDefinitionId{"raid_space.test.support_room"};
    interior.displayName = "Support Room";
    interior.worldSize = {280.0F, 240.0F};
    interior.exteriorEntrance = {{100.0F, 200.0F}, {32.0F, 32.0F}};
    interior.exteriorReturn = kPlayerStart;
    interior.interiorSpawn = {62.0F, 65.0F};
    interior.interiorExit = {{62.0F, 65.0F}, {32.0F, 32.0F}};
    config.interiors.push_back(interior);
    GameplayWorld world{config};

    ASSERT_TRUE(world.setFireSupportOrders(
        FireSupportMovementOrder::Hold, FireSupportFireOrder::HoldFire));
    for (int tick{}; tick < 4800 && !world.fireSupportDeathPosition(); ++tick)
        world.update({}, kFrame);
    const auto death = world.fireSupportDeathPosition();
    ASSERT_TRUE(death);
    expectSamePosition(*death, kCompanionStart);
    EXPECT_EQ(world.fireSupportHealth(), 0);
    EXPECT_TRUE(world.canInteractWithFireSupport());
    EXPECT_FALSE(world.setFireSupportOrders(
        FireSupportMovementOrder::Follow, FireSupportFireOrder::FreeFire));

    GameplayInput interact;
    interact.interactJustPressed = true;
    world.update(interact, 0.0F);
    EXPECT_TRUE(world.spaceTransitionedLastUpdate());
    EXPECT_FALSE(world.fireSupport()->outdoor);
    EXPECT_FALSE(world.fireSupport()->visible);
    EXPECT_FALSE(world.canInteractWithFireSupport());
    ASSERT_TRUE(world.fireSupportDeathPosition());
    expectSamePosition(*world.fireSupportDeathPosition(), *death);
}
