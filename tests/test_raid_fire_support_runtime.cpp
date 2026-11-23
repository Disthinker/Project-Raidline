#include <gtest/gtest.h>

#include "collision.h"
#include "gameplay_world.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <iostream>

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

namespace
{
    RaidInteriorWorldConfig supportRoom(std::string id = "raid_space.test.east")
    {
        RaidInteriorWorldConfig room;
        room.id = RaidSpaceDefinitionId{std::move(id)};
        room.displayName = "Cross-space test room";
        room.worldSize = {600, 400};
        room.exteriorEntrance = {kPlayerStart, {48, 48}};
        room.exteriorReturn = room.interiorSpawn = kPlayerStart;
        room.interiorExit = room.exteriorEntrance;
        return room;
    }

    GameplayInput useDoor()
    {
        GameplayInput input;
        input.interactJustPressed = true;
        return input;
    }
}

TEST(RaidFireSupportCrossSpaceTest, AllFourPublishedInteriorsHaveLegalRoundTripLandings)
{
    std::size_t checked = 0;
    for (const auto &id : {"map.raid.hospital_district", "map.raid.frontier_exchange"})
    {
        const auto &map = publishedContentRegistry().map(MapDefinitionId{id});
        for (const auto &source : map.interiors)
        {
            SCOPED_TRACE(source.id.value());
            auto config = supportWorld();
            auto room = supportRoom();
            room.id = source.id;
            room.worldSize = source.worldSize;
            room.exteriorEntrance = source.exteriorEntrance;
            room.exteriorReturn = source.exteriorReturn;
            room.interiorSpawn = source.interiorSpawn;
            room.interiorExit = source.interiorExit;
            for (const auto &blocker : source.ballisticBlockers)
                room.ballisticBlockers.push_back({room.ballisticBlockers.size() + 1U,
                    {blocker.bounds.position, blocker.bounds.size}});
            config.interiors = {room};
            config.worldSize = {std::max(1000.F, room.exteriorReturn.x + 512),
                std::max(700.F, room.exteriorReturn.y + 512)};
            config.playerSpawn = {room.exteriorEntrance.position.x + 16,
                room.exteriorEntrance.position.y + 16};
            GameplayWorld world{config};

            world.update(useDoor(), 0);
            ASSERT_EQ(world.activeRaidSpaceId(), room.id);
            ASSERT_TRUE(world.fireSupport()->sameSpace);
            const auto landing = world.fireSupport()->bounds;
            for (const auto &wall : room.ballisticBlockers)
                EXPECT_FALSE(isCollision(landing, wall.bounds));
            EXPECT_FALSE(isCollision(landing, Rect{world.player().position(), {world.player().size(), world.player().size()}}));
            EXPECT_TRUE(world.canInteractWithFireSupport());

            placePlayer(world, {room.interiorExit.position.x + room.interiorExit.size.x * 0.5F - 16,
                room.interiorExit.position.y + room.interiorExit.size.y * 0.5F - 16});
            world.update(useDoor(), 0);
            EXPECT_TRUE(world.inOutdoorRaidSpace());
            EXPECT_TRUE(world.fireSupport()->sameSpace);
            EXPECT_TRUE(world.canInteractWithFireSupport());
            ++checked;
        }
    }
    EXPECT_EQ(checked, 4U);
}

TEST(RaidFireSupportCrossSpaceTest, WaitingInOneRoomDoesNotExposeActorInIdenticalSiblingRoom)
{
    auto config = supportWorld();
    auto first = supportRoom();
    auto second = supportRoom("raid_space.test.west");
    second.exteriorEntrance.position = {370, 220};
    second.exteriorReturn = second.exteriorEntrance.position;
    config.interiors = {first, second};
    GameplayWorld world{config};

    world.update(useDoor(), 0);
    ASSERT_EQ(world.activeRaidSpaceId(), first.id);
    ASSERT_TRUE(world.setFireSupportOrders(FireSupportMovementOrder::Hold, FireSupportFireOrder::FreeFire));
    const auto held = *world.fireSupport();
    world.update(useDoor(), 0);
    ASSERT_TRUE(world.inOutdoorRaidSpace());
    ASSERT_FALSE(world.fireSupport()->sameSpace);

    placePlayer(world, second.exteriorEntrance.position);
    world.update(useDoor(), 0);
    ASSERT_EQ(world.activeRaidSpaceId(), second.id);
    world.update({}, kFrame);
    EXPECT_FALSE(world.fireSupport()->visible);
    EXPECT_FALSE(world.canInteractWithFireSupport());
    EXPECT_FALSE(world.fireSupportShotLastUpdate());
    EXPECT_FALSE(world.setFireSupportOrders(FireSupportMovementOrder::Follow, FireSupportFireOrder::HoldFire));
    world.update(useDoor(), 0);
    placePlayer(world, kPlayerStart);
    world.update(useDoor(), 0);
    ASSERT_EQ(world.activeRaidSpaceId(), first.id);
    ASSERT_TRUE(world.fireSupport()->sameSpace);
    EXPECT_TRUE(world.canInteractWithFireSupport());
    expectSamePosition(world.fireSupport()->bounds.position, held.bounds.position);
    EXPECT_EQ(world.fireSupport()->health, held.health);
    EXPECT_EQ(world.fireSupport()->movementOrder, held.movementOrder);
    EXPECT_EQ(world.fireSupport()->fireOrder, held.fireOrder);
}

TEST(RaidFireSupportCrossSpaceTest, RejectedTransitionDistantEscortAndBlockedDestinationNeverWarpCompanion)
{
    auto config = supportWorld();
    auto room = supportRoom();
    config.interiors = {room};
    GameplayWorld denied{config};
    denied.update(useDoor(), 0, {}, [] { return false; });
    EXPECT_FALSE(denied.spaceTransitionedLastUpdate());
    ASSERT_TRUE(denied.inOutdoorRaidSpace());
    expectSamePosition(center(denied.fireSupport()->bounds), kCompanionStart);

    config.interiors[0].exteriorEntrance.position = {550, 220};
    GameplayWorld distant{config};
    placePlayer(distant, config.interiors[0].exteriorEntrance.position);
    distant.update(useDoor(), 0);
    ASSERT_EQ(distant.activeRaidSpaceId(), room.id);
    EXPECT_FALSE(distant.fireSupport()->sameSpace);
    expectSamePosition(center(distant.fireSupport()->bounds), kCompanionStart);

    // The radius is measured to the portal edge, not its center or just the player.
    for (const float separation : {96.F, 96.25F})
    {
        config.interiors[0].exteriorEntrance = {{kCompanionStart.x + separation, 200}, {48, 48}};
        GameplayWorld edge{config};
        placePlayer(edge, config.interiors[0].exteriorEntrance.position);
        edge.update(useDoor(), 0);
        ASSERT_EQ(edge.activeRaidSpaceId(), room.id);
        EXPECT_EQ(edge.fireSupport()->sameSpace, separation <= 96.F);
    }
    config.interiors[0].exteriorEntrance.position = {220, 200};
    config.ballisticBlockers = {{1, {{199, 160}, {6, 120}}}};
    GameplayWorld walledOff{config};
    placePlayer(walledOff, config.interiors[0].exteriorEntrance.position);
    walledOff.update(useDoor(), 0);
    ASSERT_EQ(walledOff.activeRaidSpaceId(), room.id);
    EXPECT_FALSE(walledOff.fireSupport()->sameSpace);
    config.ballisticBlockers.clear();
    // A room with exactly one player-sized pocket leaves no legal second footprint.
    room.interiorSpawn = {16, 16};
    room.interiorExit = {{16, 16}, {32, 32}};
    room.worldSize = {64, 64};
    room.ballisticBlockers = {{1, {{0, 0}, {16, 64}}}, {2, {{48, 0}, {16, 64}}},
        {3, {{16, 0}, {32, 16}}}, {4, {{16, 48}, {32, 16}}}};
    config.interiors = {room};
    GameplayWorld blocked{config};
    blocked.update(useDoor(), 0);
    ASSERT_EQ(blocked.activeRaidSpaceId(), room.id);
    EXPECT_FALSE(blocked.fireSupport()->sameSpace);
    EXPECT_FALSE(blocked.canInteractWithFireSupport());
    blocked.update(useDoor(), 0);
    ASSERT_TRUE(blocked.inOutdoorRaidSpace());
    EXPECT_TRUE(blocked.fireSupport()->sameSpace);
    expectSamePosition(center(blocked.fireSupport()->bounds), kCompanionStart);
}

TEST(RaidFireSupportCrossSpaceTest, PortalRetainsCadenceDropsOldFlightAndUsesInteriorOcclusion)
{
    auto config = supportWorld();
    config.fireSupport->projectileSpeed = 250;
    const EnemySpawn victim{{300, 204}, {20, 20}, 100};
    config.initialEnemies = {victim};
    auto room = supportRoom();
    room.initialEnemies = {victim};
    config.interiors = {room};
    GameplayWorld world{config};
    ASSERT_TRUE(world.setFireSupportOrders(FireSupportMovementOrder::Follow, FireSupportFireOrder::FreeFire));
    world.update({}, kFrame);
    ASSERT_TRUE(world.fireSupportShotLastUpdate());
    world.update(useDoor(), 0);
    ASSERT_TRUE(world.fireSupport()->sameSpace);
    ASSERT_EQ(world.activeRaidSpaceId(), room.id);
    ASSERT_TRUE(world.setFireSupportOrders(FireSupportMovementOrder::Hold, FireSupportFireOrder::FreeFire));
    for (int i = 0; i < 62; ++i)
    {
        world.update({}, kFrame);
        ASSERT_FALSE(world.fireSupportShotLastUpdate());
        ASSERT_EQ(world.enemies().front().health(), victim.maxHealth);
    }
    bool resumed = false;
    for (int i = 0; i < 30 && !resumed; ++i)
    {
        world.update({}, kFrame);
        resumed = world.fireSupportShotLastUpdate().has_value();
    }
    EXPECT_TRUE(resumed);

    config.initialEnemies.clear();
    config.interiors[0].ballisticBlockers = {{1, {{240, 0}, {8, 400}}}};
    GameplayWorld occluded{config};
    occluded.update(useDoor(), 0);
    ASSERT_TRUE(occluded.fireSupport()->sameSpace);
    ASSERT_TRUE(occluded.setFireSupportOrders(FireSupportMovementOrder::Hold, FireSupportFireOrder::FreeFire));
    for (int i = 0; i < 30; ++i)
    {
        occluded.update({}, kFrame);
        EXPECT_FALSE(occluded.fireSupportShotLastUpdate());
    }
    EXPECT_EQ(occluded.enemies().front().health(), victim.maxHealth);
}

TEST(RaidFireSupportCrossSpaceTest, WoundedFollowerRetainsExistingHealthOnBothSidesOfDoor)
{
    auto config = supportWorld();
    const auto room = supportRoom();
    config.interiors = {room};
    config.initialEnemies = {EnemySpawn{{215, 197}, {38, 38}, 100}};
    GameplayWorld world{config};
    ASSERT_TRUE(world.setFireSupportOrders(FireSupportMovementOrder::Hold, FireSupportFireOrder::HoldFire));
    for (int tick = 0; tick < 180 && world.fireSupportHealth() == kFireSupportMaxHealth; ++tick)
        world.update({}, kFrame);
    ASSERT_GT(*world.fireSupportHealth(), 0);
    ASSERT_LT(*world.fireSupportHealth(), kFireSupportMaxHealth);
    const auto injured = world.fireSupportHealth();
    ASSERT_TRUE(world.setFireSupportOrders(FireSupportMovementOrder::Follow, FireSupportFireOrder::HoldFire));
    world.update(useDoor(), 0);
    ASSERT_EQ(world.fireSupportSpaceId(), room.id);
    EXPECT_EQ(world.fireSupportHealth(), injured);
    world.update(useDoor(), 0);
    ASSERT_EQ(world.fireSupportSpaceId(), outdoorRaidSpaceId());
    EXPECT_EQ(world.fireSupportHealth(), injured);
}
TEST(RaidFireSupportCrossSpaceTest, IndoorDeathRemainsInItsOwnRoomAcrossExitAndReentry)
{
    auto config = supportWorld();
    auto room = supportRoom();
    room.initialEnemies = {EnemySpawn{{215, 197}, {38, 38}, 100}};
    config.interiors = {room};
    GameplayWorld world{config};
    world.update(useDoor(), 0);
    ASSERT_TRUE(world.fireSupport()->sameSpace);
    ASSERT_TRUE(world.setFireSupportOrders(FireSupportMovementOrder::Hold, FireSupportFireOrder::HoldFire));
    for (int i = 0; i < 4800 && !world.fireSupportDeathPosition(); ++i)
        world.update({}, kFrame);
    ASSERT_TRUE(world.fireSupportDeathPosition());
    const auto death = *world.fireSupportDeathPosition();
    EXPECT_EQ(world.fireSupportSpaceId(), room.id);
    EXPECT_EQ(world.fireSupportHealth(), 0);
    world.update(useDoor(), 0);
    ASSERT_TRUE(world.inOutdoorRaidSpace());
    EXPECT_EQ(world.fireSupportSpaceId(), room.id);
    EXPECT_FALSE(world.fireSupport()->sameSpace);
    EXPECT_FALSE(world.canInteractWithFireSupport());
    world.update(useDoor(), 0);
    ASSERT_EQ(world.activeRaidSpaceId(), room.id);
    EXPECT_TRUE(world.canInteractWithFireSupport());
    ASSERT_TRUE(world.fireSupportDeathPosition());
    expectSamePosition(*world.fireSupportDeathPosition(), death);
}

TEST(RaidFireSupportCrossSpaceTest, MeasuresDenseIndoorSimulationAndPortalFramesWithEscortPresent)
{
    using Clock = std::chrono::steady_clock;
    for (int count : {32, 100})
    {
        SCOPED_TRACE(count);
        auto config = supportWorld();
        config.lightConeObservationEnabled = true;
        auto room = supportRoom();
        room.worldSize = {1800, 1300};
        room.interiorSpawn = {884, 634};
        const Vec2 origin{900, 650};
        room.interiorExit = {{840, 590}, {120, 120}};
        for (int i = 0; i < count; ++i)
        {
            const float a = static_cast<float>(i) * 6.2831853F / count;
            const float radius = 210.F + 30.F * (i % 3);
            room.initialEnemies.push_back(EnemySpawn{{origin.x + radius * std::cos(a),
                origin.y + radius * std::sin(a)}, {20, 20}, 100});
        }
        for (int i = 0; i < 2304 && room.ballisticBlockers.size() < 220; ++i)
        {
            // Cycle through the full local grid. Do not concentrate walls in one far row.
            const int cell = (i * 491) % 2304;
            const Rect wall{{origin.x - 336 + (cell % 48) * 14, origin.y - 336 + (cell / 48) * 14}, {6, 9}};
            if (isCollision(wall, {{origin.x - 64, origin.y - 96}, {176, 192}}) ||
                std::any_of(room.initialEnemies.begin(), room.initialEnemies.end(), [&](const EnemySpawn &enemy)
                    { return isCollision(wall, {enemy.position, enemy.size}); }))
                continue;
            room.ballisticBlockers.push_back({static_cast<BallisticBlockerId>(i + 1), wall});
        }
        ASSERT_EQ(room.ballisticBlockers.size(), 220U);
        config.interiors = {room};
        std::vector<double> entryTimes, frameTimes;
        std::size_t observed = 0;
        for (int i = 0; i < 160; ++i)
        {
            GameplayWorld world{config};
            ASSERT_TRUE(world.setFireSupportOrders(FireSupportMovementOrder::Follow, FireSupportFireOrder::FreeFire));
            const auto entryStart = Clock::now();
            world.update(useDoor(), 0);
            const auto entryEnd = Clock::now();
            ASSERT_TRUE(world.spaceTransitionedLastUpdate());
            ASSERT_EQ(world.activeRaidSpaceId(), room.id);
            ASSERT_TRUE(world.fireSupport()->sameSpace);

            GameplayInput aim;
            const float angle = i * 0.13F;
            aim.aimWorldPosition = {origin.x + 200 * std::cos(angle), origin.y + 200 * std::sin(angle)};
            aim.moveLeft = true;
            const auto frameStart = Clock::now();
            world.update(aim, kFrame);
            const bool valid = world.raidObservation().validGeometry();
            for (const auto &enemy : world.enemies())
                observed += world.raidEnemyObservable(enemy.combatTargetId()) ? 1U : 0U;
            const auto frameEnd = Clock::now();
            ASSERT_TRUE(valid);
            ASSERT_EQ(world.simulationWorkloadLastUpdate().activeBlockers, 220U);
            ASSERT_EQ(world.simulationWorkloadLastUpdate().activeEnemies, static_cast<std::size_t>(count));
            if (i >= 10)
            {
                entryTimes.push_back(std::chrono::duration<double, std::milli>(entryEnd - entryStart).count());
                frameTimes.push_back(std::chrono::duration<double, std::milli>(frameEnd - frameStart).count());
            }
        }
        ASSERT_GT(observed, 0U);
        std::sort(entryTimes.begin(), entryTimes.end());
        std::sort(frameTimes.begin(), frameTimes.end());
        for (const auto &[key, ms] : {std::pair{"portal", entryTimes[142]}, std::pair{"indoor", frameTimes[142]}})
        {
            RecordProperty("support_" + std::to_string(count) + "_" + key + "_p95_ms", std::to_string(ms));
            EXPECT_LE(ms, 3.0) << key << ": enemies=" << count << " blockers=220 samples=150";
            std::cout << "Fire support " << key << ": enemies=" << count << " blockers=220 samples=150 p95_ms=" << ms << '\n';
        }
    }
}
