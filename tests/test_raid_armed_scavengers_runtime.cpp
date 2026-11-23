#include "gameplay_world.h"
#include "collision.h"

#include <gtest/gtest.h>
#include <cmath>

namespace
{
constexpr float kFrame = 1.0F / 60.0F;
RaidWorldConfig arena()
{
    RaidWorldConfig config;
    config.worldSize = {960, 560};
    config.playerSpawn = {450, 200};
    config.extractionPoint = {{880, 470}, {70, 70}};
    config.deferPlayerDamageResolution = true;
    EnemySpawn guard{{200, 200}, {32, 32}, 24};
    guard.sourceEnemyId = "riverside-scavenger:0";
    guard.armedScavenger = RaidArmedScavengerConfig{{8, 1400, 4, 360}, 6, 6, false};
    config.initialEnemies.push_back(guard);
    return config;
}

void step(GameplayWorld &world, int frames)
{
    for (int i{}; i < frames; ++i) world.update({}, kFrame);
}

void placePlayer(GameplayWorld &world, Vec2 position)
{
    ASSERT_TRUE(const_cast<Player &>(world.player()).setPosition(position));
}
}

TEST(RiversideArmedScavengerRuntimeTest, SightLossRestartsTelegraphAndPistolDamageUsesStableIdentity)
{
    auto config = arena();
    config.ballisticBlockers = {{1, {{325, 280}, {20, 190}}}};
    GameplayWorld world{config};
    step(world, 40);
    const auto &guard = world.armedScavengers().front();
    ASSERT_TRUE(guard.aiming);
    EXPECT_EQ(guard.shotsFired, 0);
    EXPECT_GT(guard.aimDirection.x, 0.9F);

    placePlayer(world, {450, 420});
    step(world, 90);
    EXPECT_FALSE(guard.aiming);
    EXPECT_EQ(guard.shotsFired, 0);
    EXPECT_TRUE(world.takePlayerDamageObservations().empty());
    placePlayer(world, {450, 200});
    step(world, 40);
    EXPECT_EQ(guard.shotsFired, 0);
    for (int i{}; i < 20 && guard.shotsFired == 0; ++i) world.update({}, kFrame);
    ASSERT_EQ(world.armedScavengerShotsLastUpdate().size(), 1U);
    EXPECT_TRUE(guard.ammunitionChangedLastUpdate);
    EXPECT_EQ(guard.loadedRounds, 5);
    EXPECT_EQ(guard.spareRounds, 6);
    EXPECT_FALSE(world.shotFiredLastUpdate());
    step(world, 20);
    EXPECT_FALSE(guard.ammunitionChangedLastUpdate);
    const auto hits = world.takePlayerDamageObservations();
    ASSERT_EQ(hits.size(), 1U);
    EXPECT_EQ(hits[0].sourceEnemyId, guard.id);
    EXPECT_EQ(hits[0].baseDamage, 8);
    EXPECT_EQ(hits[0].penetration, 4);
    EXPECT_FALSE(hits[0].attackType);
    EXPECT_FALSE(world.player().isControlled());
    EXPECT_FLOAT_EQ(world.enemies().front().position().x, 200);
    EXPECT_FLOAT_EQ(world.enemies().front().position().y, 200);
}

TEST(RiversideArmedScavengerRuntimeTest, RetreatAtOneThirdHealthWalksAroundCoverAndCommitsWithoutDeath)
{
    auto config = arena();
    const Vec2 exit{620, 215};
    config.initialEnemies.front().armedScavenger->retreatPoint = exit;
    const Rect wall{{320, 95}, {100, 230}};
    config.ballisticBlockers = {{1, wall}};
    GameplayWorld world{config};
    auto &actor = const_cast<Enemy &>(world.enemies().front());
    const auto &view = world.armedScavengers().front();
    static_cast<void>(actor.takeDamage(15));
    world.update({}, kFrame);
    EXPECT_FALSE(view.retreating);
    EXPECT_FALSE(world.confirmArmedScavengerEscape(view.sourceEnemyId));
    static_cast<void>(actor.takeDamage(1));
    const int score = world.score(), initialShots = view.shotsFired;
    bool wentAround{};
    for (int frame = 0; frame < 650 && !view.escapeReady; ++frame)
    {
        const auto before = actor.position();
        world.update({}, kFrame);
        ASSERT_TRUE(view.alive && view.retreating);
        const auto now = actor.position();
        EXPECT_LE(std::hypot(now.x - before.x, now.y - before.y), 110 * kFrame + 0.002F);
        EXPECT_FALSE(isCollision(actor.bounds(), wall));
        wentAround |= now.y + actor.size().y <= wall.position.y || now.y >= wall.position.y + wall.size.y;
    }
    EXPECT_TRUE(wentAround);
    ASSERT_TRUE(view.escapeReady);
    EXPECT_FALSE(view.aiming || view.reloading || view.escaped);
    EXPECT_EQ(view.shotsFired, initialShots);
    EXPECT_EQ(world.enemies().size(), 1U);
    EXPECT_FALSE(world.confirmArmedScavengerEscape("other"));
    ASSERT_TRUE(world.confirmArmedScavengerEscape(view.sourceEnemyId));
    EXPECT_TRUE(view.escaped);
    EXPECT_FALSE(view.alive || view.retreating || view.escapeReady);
    EXPECT_FALSE(world.confirmArmedScavengerEscape(view.sourceEnemyId));
    EXPECT_TRUE(world.enemies().empty());
    EXPECT_TRUE(world.pendingCorpseDeaths().empty());
    EXPECT_EQ(world.score(), score);
}

TEST(RiversideArmedScavengerRuntimeTest, EmptyErasedMagazineDoesNotTriggerWhileSpareExistsAndOldGuardStays)
{
    auto config = arena();
    config.initialEnemies.front().armedScavenger->loadedRounds = 0;
    config.initialEnemies.front().armedScavenger->retreatPoint = Vec2{510, 215};
    GameplayWorld reserve{config};
    reserve.update({}, kFrame);
    EXPECT_FALSE(reserve.armedScavengers().front().retreating);
    EXPECT_TRUE(reserve.armedScavengers().front().reloading);

    auto &gun = *config.initialEnemies.front().armedScavenger;
    gun.spareRounds = 0;
    gun.spareMagazineInstalled = true;
    GameplayWorld empty{config};
    empty.update({}, kFrame);
    EXPECT_TRUE(empty.armedScavengers().front().retreating);
    EXPECT_GT(empty.enemies().front().position().x, config.initialEnemies.front().position.x);

    gun.retreatPoint.reset();
    GameplayWorld old{config};
    static_cast<void>(const_cast<Enemy &>(old.enemies().front()).takeDamage(20));
    step(old, 90);
    EXPECT_FALSE(old.armedScavengers().front().retreating);
    EXPECT_EQ(old.enemies().front().position().x, config.initialEnemies.front().position.x);
}

TEST(RiversideArmedScavengerRuntimeTest, RetreatPausesIndoorsAndLethalHitBeatsReadyEscape)
{
    auto config = arena();
    auto &gun = *config.initialEnemies.front().armedScavenger;
    gun.retreatPoint = Vec2{300, 216};
    gun.loadedRounds = gun.spareRounds = 0;
    gun.spareMagazineInstalled = true;
    RaidInteriorWorldConfig room;
    room.id = RaidSpaceDefinitionId{"raid_space.test.retreat"};
    room.displayName = "Retreat test room";
    room.worldSize = {600, 400};
    room.exteriorEntrance = {config.playerSpawn, {48, 48}};
    room.exteriorReturn = config.playerSpawn;
    room.interiorSpawn = {60, 60};
    room.interiorExit = {room.interiorSpawn, {48, 48}};
    config.interiors.push_back(room);
    GameplayWorld world{config};
    const auto &view = world.armedScavengers().front();
    step(world, 10);
    const Vec2 beforeDoor = world.enemies().front().position();
    GameplayInput interact;
    interact.interactJustPressed = true;
    world.update(interact, 0);
    ASSERT_EQ(world.activeRaidSpaceId(), room.id);
    step(world, 140);
    EXPECT_FALSE(view.escapeReady);
    world.update(interact, 0);
    ASSERT_EQ(world.activeRaidSpaceId(), outdoorRaidSpaceId());
    EXPECT_FLOAT_EQ(world.enemies().front().position().x, beforeDoor.x);
    step(world, 55);
    ASSERT_TRUE(view.escapeReady);
    auto &actor = const_cast<Enemy &>(world.enemies().front());
    static_cast<void>(actor.takeDamage(actor.health()));
    EXPECT_FALSE(world.confirmArmedScavengerEscape(view.sourceEnemyId));
    world.update({}, kFrame);
    ASSERT_FALSE(view.alive);
    EXPECT_FALSE(view.escaped || view.escapeReady);
    ASSERT_EQ(world.pendingCorpseDeaths().size(), 1U);
    EXPECT_EQ(world.pendingCorpseDeaths().front().sourceEnemyId, view.sourceEnemyId);
    EXPECT_GT(world.score(), 0);
}

TEST(RiversideArmedScavengerRuntimeTest, UnreachableRetreatNeverTeleportsOrCreatesAnEscape)
{
    auto config = arena();
    auto &gun = *config.initialEnemies.front().armedScavenger;
    gun.retreatPoint = Vec2{570, 210};
    gun.loadedRounds = gun.spareRounds = 0;
    config.ballisticBlockers = {{1, {{330, 0}, {50, 560}}}};
    GameplayWorld world{config};
    step(world, 500);
    const auto &view = world.armedScavengers().front();
    EXPECT_TRUE(view.alive && view.retreating);
    EXPECT_FALSE(view.escapeReady || view.escaped);
    EXPECT_LT(world.enemies().front().position().x, 330);
    EXPECT_FALSE(world.confirmArmedScavengerEscape(view.sourceEnemyId));
    EXPECT_TRUE(world.pendingCorpseDeaths().empty());
}

TEST(RiversideArmedScavengerRuntimeTest, UsesOnlyOriginalTwelveRoundsAndOneTimedMagazineChange)
{
    GameplayWorld world{arena()};
    const auto &guard = world.armedScavengers().front();
    for (int i{}; i < 400 && guard.loadedRounds != 0; ++i) world.update({}, kFrame);
    ASSERT_EQ(guard.shotsFired, 6);
    ASSERT_TRUE(guard.reloading);
    step(world, 149);
    EXPECT_EQ(guard.loadedRounds, 0);
    EXPECT_EQ(guard.spareRounds, 6);
    EXPECT_FALSE(guard.spareMagazineInstalled);
    for (int i{}; i < 4 && !guard.spareMagazineInstalled; ++i) world.update({}, kFrame);
    ASSERT_TRUE(guard.ammunitionChangedLastUpdate);
    EXPECT_EQ(guard.loadedRounds, 6);
    EXPECT_EQ(guard.spareRounds, 0);
    EXPECT_FALSE(guard.reloading);
    step(world, 720);
    EXPECT_EQ(guard.shotsFired, 12);
    EXPECT_EQ(guard.loadedRounds, 0);
    EXPECT_FALSE(guard.reloading);
    EXPECT_FALSE(guard.aiming);
    EXPECT_EQ(world.takePlayerDamageObservations().size(), 12U);
}

TEST(RiversideArmedScavengerRuntimeTest, FreshGunshotAttractsInfectedBehindCoverAndGuardsCannotShootEachOther)
{
    auto config = arena();
    config.ballisticBlockers = {
        {1, {{150, 250}, {140, 12}}}, {2, {{280, 262}, {10, 72}}},
        {3, {{150, 262}, {10, 72}}}, {4, {{150, 334}, {140, 10}}}};
    EnemySpawn otherGuard = config.initialEnemies.front();
    otherGuard.sourceEnemyId = "riverside-scavenger:1";
    otherGuard.position = {235, 200};
    config.initialEnemies.push_back(otherGuard);
    config.initialEnemies.push_back({{204, 278}, {20, 20}, 100});
    GameplayWorld world{config};
    for (int i{}; i < 70 && world.armedScavengerShotsLastUpdate().empty(); ++i) world.update({}, kFrame);
    ASSERT_EQ(world.armedScavengerShotsLastUpdate().size(), 2U);
    const auto heard = world.enemies().back().lastKnownTargetPosition();
    ASSERT_TRUE(heard);
    EXPECT_FLOAT_EQ(heard->x, 251);
    EXPECT_FLOAT_EQ(heard->y, 216);
    step(world, 15);
    EXPECT_EQ(world.enemies()[0].health(), 24);
    EXPECT_EQ(world.enemies()[1].health(), 24);
    EXPECT_EQ(world.takePlayerDamageObservations().size(), 2U);
}

TEST(RiversideArmedScavengerRuntimeTest, NearbyInfectedWinContactAndDeathPreventsFurtherFireOrDuplicateCorpse)
{
    auto config = arena();
    config.playerSpawn = {820, 330};
    // A low-damage test pistol gives the ordinary 12-HP variant time to close.
    config.initialEnemies.front().armedScavenger->pistol.damage = 1;
    EnemySpawn infected{{365, 200}, {32, 32}, 12};
    infected.variant = ordinaryEnemyVariantSnapshot();
    config.initialEnemies.push_back(infected);
    GameplayWorld world{config};
    const auto &guard = world.armedScavengers().front();
    bool infectedHit = false, guardHit = false;
    for (int i{}; i < 1200 && guard.alive; ++i)
    {
        world.update({}, kFrame);
        infectedHit = infectedHit || world.enemies().back().health() < 12;
        guardHit = guardHit || (guard.alive && world.enemies().front().health() < 24);
    }
    ASSERT_FALSE(guard.alive);
    EXPECT_TRUE(infectedHit);
    EXPECT_TRUE(guardHit);
    const auto spent = guard.shotsFired;
    ASSERT_EQ(world.pendingCorpseDeaths().size(), 1U);
    EXPECT_EQ(world.pendingCorpseDeaths().front().sourceEnemyId, guard.sourceEnemyId);
    world.acknowledgeCorpseDeath(guard.sourceEnemyId);
    step(world, 180);
    EXPECT_EQ(guard.shotsFired, spent);
    EXPECT_FALSE(guard.aiming);
    EXPECT_TRUE(world.pendingCorpseDeaths().empty());
    EXPECT_TRUE(world.takePlayerDamageObservations().empty());
}

TEST(RiversideArmedScavengerRuntimeTest, ReloadedTrailingRoundsResumeWithoutRefilling)
{
    auto config = arena();
    auto &weapon = *config.initialEnemies.front().armedScavenger;
    weapon.loadedRounds = 2;
    weapon.spareRounds = 0;
    weapon.spareMagazineInstalled = true;
    GameplayWorld world{config};
    const auto &guard = world.armedScavengers().front();
    EXPECT_EQ(guard.shotsFired, 10);
    step(world, 240);
    EXPECT_EQ(guard.shotsFired, 12);
    EXPECT_EQ(world.takePlayerDamageObservations().size(), 2U);
}

TEST(RiversideArmedScavengerRuntimeTest, MovingEitherSightEndpointChangesAnInfectedsTarget)
{
    auto config = arena();
    config.playerSpawn = {570, 200};
    config.ballisticBlockers = {{1, {{280, 235}, {20, 80}}}};
    config.initialEnemies.push_back({{350, 200}, {32, 32}, 12});
    GameplayWorld world{config};
    world.update({}, kFrame);
    auto &infected = const_cast<Enemy &>(world.enemies().back());
    ASSERT_TRUE(infected.lastKnownTargetPosition());
    EXPECT_FLOAT_EQ(infected.lastKnownTargetPosition()->x, 216); // Guard north of the wall.

    ASSERT_TRUE(infected.setPosition({350, 300}));
    world.update({}, kFrame);
    ASSERT_TRUE(infected.lastKnownTargetPosition());
    const auto &player = world.player();
    EXPECT_FLOAT_EQ(infected.lastKnownTargetPosition()->x, player.position().x + player.size() * 0.5F);

    // The same player is now behind the wall, closer than the guard, but unseen.
    placePlayer(world, {230, 250});
    world.update({}, kFrame);
    EXPECT_EQ(infected.awarenessState(), EnemyAwarenessState::Searching);
    EXPECT_FLOAT_EQ(infected.lastKnownTargetPosition()->x, 570 + player.size() * 0.5F);
}

TEST(RiversideArmedScavengerRuntimeTest, PortalClearsOldAimAndFreezesWeaponUntilOutside)
{
    auto config = arena();
    RaidInteriorWorldConfig room;
    room.id = RaidSpaceDefinitionId{"raid_space.test.riverside"};
    room.displayName = "Riverside test room";
    room.worldSize = {600, 400};
    room.exteriorEntrance = {config.playerSpawn, {48, 48}};
    room.exteriorReturn = config.playerSpawn;
    room.interiorSpawn = {60, 60};
    room.interiorExit = {room.interiorSpawn, {48, 48}};
    config.interiors.push_back(room);
    GameplayWorld world{config};
    const auto &guard = world.armedScavengers().front();
    step(world, 40);
    ASSERT_TRUE(guard.aiming);
    GameplayInput interact;
    interact.interactJustPressed = true;
    world.update(interact, 0);
    ASSERT_EQ(world.activeRaidSpaceId(), room.id);
    ASSERT_FALSE(guard.aiming);
    step(world, 240);
    EXPECT_EQ(guard.shotsFired, 0);
    EXPECT_TRUE(world.armedScavengerShotsLastUpdate().empty());
    world.update(interact, 0);
    ASSERT_TRUE(world.inOutdoorRaidSpace());
    step(world, 20);
    EXPECT_EQ(guard.shotsFired, 0);
    step(world, 40);
    EXPECT_EQ(guard.shotsFired, 1);
}

TEST(RiversideArmedScavengerRuntimeTest, ShotHitsNearestPhysicalBodyAndCoverWinsExactTies)
{
    EnemyRoster<> actors;
    actors.spawn(Enemy{{0, 10}, {20, 20}, {}, 24, 1});
    actors.back().markArmedScavenger();
    actors.spawn(Enemy{{70, 10}, {20, 20}, {}, 24, 2});
    const Rect player{{110, 10}, {20, 20}};
    const ShotCollisionCandidate shot{1, {10, 20}, {180, 20}, 2, 8};
    const auto blocked = resolveShotHits({shot}, actors, {{1, actors[1].bounds()}}, player);
    ASSERT_EQ(blocked.hits.size(), 1U);
    EXPECT_EQ(blocked.hits[0].targetKind, HitTargetKind::Obstacle);
    EXPECT_FALSE(blocked.playerImpact);
    EXPECT_EQ(actors[1].health(), 24);
    const auto intercepted = resolveShotHits({shot}, actors, {}, player);
    ASSERT_EQ(intercepted.hits.size(), 1U);
    EXPECT_EQ(intercepted.hits[0].targetId, 2U);
    EXPECT_FALSE(intercepted.playerImpact);
    const auto nearPlayer = resolveShotHits({shot}, actors, {}, Rect{{45, 10}, {20, 20}});
    EXPECT_EQ(nearPlayer.playerImpact, HitRegion::Torso);
    EXPECT_TRUE(nearPlayer.hits.empty());
    EXPECT_EQ(actors[0].health(), 24);
    EXPECT_EQ(actors[1].health(), 16);
}

TEST(RiversideArmedScavengerRuntimeTest, FiredRoundTravelsAfterShooterDeathWithoutAnotherShot)
{
    auto config = arena();
    config.initialEnemies.front().armedScavenger->pistol.projectileSpeed = 300;
    GameplayWorld world{config};
    const auto &guard = world.armedScavengers().front();
    for (int i{}; i < 80 && guard.shotsFired == 0; ++i) world.update({}, kFrame);
    ASSERT_EQ(guard.shotsFired, 1);
    ASSERT_TRUE(world.takePlayerDamageObservations().empty());
    // Stage a lethal hit after discharge, before the next travelled segment.
    ASSERT_TRUE(const_cast<Enemy &>(world.enemies().front()).takeDamage(24));
    world.update({}, kFrame);
    ASSERT_FALSE(guard.alive);
    ASSERT_TRUE(world.takePlayerDamageObservations().empty());
    step(world, 180);
    const auto hits = world.takePlayerDamageObservations();
    ASSERT_EQ(hits.size(), 1U);
    EXPECT_EQ(hits[0].sourceEnemyId, guard.id);
    EXPECT_EQ(hits[0].baseDamage, 8);
    EXPECT_EQ(guard.shotsFired, 1);
    EXPECT_EQ(guard.loadedRounds, 5);
    EXPECT_EQ(world.pendingCorpseDeaths().size(), 1U);
}
