#include "base_defense_runtime.h"
#include "alpha_content_ids.h"
#include "base_world.h"
#include "combat_runtime_checkpoint_json.h"
#include "home_founding_domain.h"
#include <algorithm>
#include <cmath>
#include <gtest/gtest.h>

namespace
{
BaseDefenseSnapshot seed()
{
    BaseDefenseSnapshot s;
    s.eventId = "test-defense-1";
    s.siegeSequence = 1;
    s.seed = 12345;
    s.frozenPopulation = 8;
    s.frozenMoraleTier = 1;
    s.frozenSiteThreat = 1;
    return s;
}
std::optional<BaseDefenseSnapshot> arena()
{
    auto s = seed();
    s.siteDefinitionId = "regional_base_site.greyline_yard";
    s.layoutIdentity = "runtime-arena";
    s.worldSize = {6000, 6000};
    s.safeCore = {{2300, 2300}, {1200, 1000}};
    s.playerPosition = {2800, 2700};
    return BaseDefenseRuntime::prepare(s, {}, publishedContentRegistry().enemyCombatDefinition(ordinaryInfectedDefinitionId()));
}
} // namespace

TEST(BaseDefenseRuntimeTest, EnemyCheckpointPreservesAttackAndConsumedHit)
{
    Enemy original{{100, 100}, {32, 48}, {}, 100, 7};
    original.hearTarget({140, 124});
    ASSERT_TRUE(original.tryStartAttack(EnemyAttackType::Scratch, {1, 0}));
    static_cast<void>(original.update(0.2F, 3000, 3000));
    auto snapshot = original.checkpoint();
    nlohmann::json json = snapshot;
    snapshot = json.get<EnemyRuntimeCheckpoint>();
    auto restored = Enemy::restoreCheckpoint(snapshot);
    ASSERT_TRUE(restored);
    EXPECT_EQ(restored->checkpoint(), original.checkpoint());
    for (unsigned i = 0; i < 60; ++i)
    {
        static_cast<void>(original.update(0.01F, 3000, 3000));
        static_cast<void>(restored->update(0.01F, 3000, 3000));
        EXPECT_EQ(original.consumeAttackHit(), restored->consumeAttackHit());
        EXPECT_EQ(original.checkpoint(), restored->checkpoint());
    }
}

TEST(BaseDefenseRuntimeTest, ShootingCheckpointPreservesFlightAndRandomCadence)
{
    WorldShootingRuntime first, second;
    GameplayInput input;
    input.aimWorldPosition = Vec2{2781, 1927};
    input.firePressed = true;
    EnemyRoster<> targets1, targets2;
    const std::vector<BallisticBlocker> blockers;
    first.beginFrame(0.001F);
    first.updateAim(input, {1000, 1000}, {1, 0}, {6000, 6000}, 0.001F);
    static_cast<void>(first.advanceShots(input, 0.001F, {1000, 1000}, 52, false, false,
                                         {6000, 6000}, targets1, blockers));
    auto saved = first.checkpoint();
    ASSERT_EQ(saved.flights.size(), 1);
    ASSERT_TRUE(validateWorldShootingCheckpoint(saved));
    nlohmann::json json = saved;
    ASSERT_TRUE(second.restoreCheckpoint(json.get<WorldShootingCheckpoint>()));
    EXPECT_EQ(first.checkpoint(), second.checkpoint());
    for (unsigned i = 0; i < 120; ++i)
    {
        first.beginFrame(0.01F);
        second.beginFrame(0.01F);
        first.updateAim(input, {1000, 1000}, {1, 0}, {6000, 6000}, 0.01F);
        second.updateAim(input, {1000, 1000}, {1, 0}, {6000, 6000}, 0.01F);
        static_cast<void>(first.advanceShots(input, 0.01F, {1000, 1000}, 52, false, false,
                                             {6000, 6000}, targets1, blockers));
        static_cast<void>(second.advanceShots(input, 0.01F, {1000, 1000}, 52, false, false,
                                              {6000, 6000}, targets2, blockers));
        EXPECT_EQ(first.checkpoint(), second.checkpoint());
    }
}

TEST(BaseDefenseRuntimeTest, SitesAndFoundingPlotsHaveResumableDeterministicRoutes)
{
    for (const char *site :
         {"regional_base_site.greyline_yard", "regional_base_site.ashworks_logistics_yard"})
    {
        BaseWorld world;
        world.configureSite(site);
        auto first = world.prepareBaseDefenseSnapshot(seed(), publishedContentRegistry().enemyCombatDefinition(ordinaryInfectedDefinitionId()));
        auto second = world.prepareBaseDefenseSnapshot(seed(), publishedContentRegistry().enemyCombatDefinition(ordinaryInfectedDefinitionId()));
        ASSERT_TRUE(first) << site;
        ASSERT_TRUE(second);
        EXPECT_EQ(first->layoutHash, second->layoutHash);
        ASSERT_TRUE(world.resumeBaseDefense(*first)) << site;
        EXPECT_EQ(world.baseDefenseEnemies().size(), 0);
    }
    for (const auto &plot : homePlotDefinitions())
    {
        BaseWorld world;
        world.configureSite("regional_base_site.greyline_yard", {}, std::string{plot.id});
        auto first = world.prepareBaseDefenseSnapshot(seed(), publishedContentRegistry().enemyCombatDefinition(ordinaryInfectedDefinitionId()));
        ASSERT_TRUE(first) << plot.id;
        ASSERT_TRUE(world.resumeBaseDefense(*first)) << plot.id;
    }
}

TEST(BaseDefenseRuntimeTest, HidingInCoreDoesNotStopBreachesOrDamageSafePlayer)
{
    BaseWorld world;
    auto saved = world.prepareBaseDefenseSnapshot(seed(), publishedContentRegistry().enemyCombatDefinition(ordinaryInfectedDefinitionId()));
    ASSERT_TRUE(saved);
    ASSERT_TRUE(world.resumeBaseDefense(*saved));
    for (unsigned i = 0; i < 7200 && world.baseDefenseState()->breachedIds.size() < 6; ++i)
    {
        static_cast<void>(world.update({}, 1.0F / 30.0F));
        ASSERT_EQ(world.baseDefenseDamageLastUpdate(), 0);
        ASSERT_LE(world.baseDefenseEnemies().size(), 16);
        ASSERT_LE(world.baseDefenseMetrics().navigationQueries,
                  world.baseDefenseMetrics().substeps);
    }
    EXPECT_GE(world.baseDefenseState()->breachedIds.size(), 6);
    std::string reason;
    EXPECT_TRUE(validateBaseDefenseSnapshot(*world.baseDefenseCheckpoint(), reason)) << reason;
}

TEST(BaseDefenseRuntimeTest, ContinuousGunfireBesideCoreCannotStrandTheWholeWave)
{
    for (unsigned side = 0; side < 4; ++side)
    {
        auto saved = arena();
        ASSERT_TRUE(saved);
        const auto core = saved->safeCore;
        const Vec2 mid{core.position.x + core.size.x / 2, core.position.y + core.size.y / 2};
        // Just inside the protected core boundary. Gunfire must not replace the
        // reachable defense-line objective with this forbidden destination.
        const Vec2 pc = side == 0 ? Vec2{core.position.x + 24, mid.y}
            : side == 1 ? Vec2{core.position.x + core.size.x - 24, mid.y}
            : side == 2 ? Vec2{mid.x, core.position.y + 28}
                        : Vec2{mid.x, core.position.y + core.size.y - 28};
        const Vec2 player{pc.x - 20, pc.y - 26};
        saved->playerPosition = player;
        BaseDefenseRuntime runtime;
        ASSERT_TRUE(runtime.resume(*saved, {}));
        WorldShootingRuntime shooting;
        for (unsigned frame = 0; frame < 7200 && !runtime.breached(); ++frame)
        {
            GameplayInput input;
            input.firePressed = true;
            input.aimWorldPosition = mid;
            shooting.beginFrame(1.0F / 30);
            shooting.updateAim(input, pc, {1, 0}, saved->worldSize, 1.0F / 30);
            runtime.advance(input, 1.0F / 30, player, {40, 52}, false, shooting, {});
        }
        EXPECT_TRUE(runtime.breached()) << side << " active=" << runtime.enemies().size();
    }
}

TEST(BaseDefenseRuntimeTest, UnreachableGunfireTargetDoesNotCancelDefenseLinePressure)
{
    auto saved = arena();
    ASSERT_TRUE(saved);
    const auto target = saved->wavePlans.front().target;
    const auto entry = saved->wavePlans.front().entry;
    const Vec2 pc{target.x + (entry.x - target.x) * 0.4F + 180,
                  target.y + (entry.y - target.y) * 0.4F + 180};
    const Vec2 player{pc.x - 20, pc.y - 26};
    const std::vector<BallisticBlocker> walls{
        {1, {{pc.x - 80, pc.y - 80}, {160, 20}}},
        {2, {{pc.x - 80, pc.y + 60}, {160, 20}}},
        {3, {{pc.x - 80, pc.y - 60}, {20, 120}}},
        {4, {{pc.x + 60, pc.y - 60}, {20, 120}}}};
    saved->playerPosition = player;
    BaseDefenseRuntime runtime;
    ASSERT_TRUE(runtime.resume(*saved, walls));
    ASSERT_TRUE(runtime.playerExposed(pc));
    WorldShootingRuntime shooting;
    auto config = shooting.checkpoint();
    config.fireConfig[0] = 0.01F;
    ASSERT_TRUE(shooting.restoreCheckpoint(config));
    for (unsigned frame = 0; frame < 6000 && !runtime.breached(); ++frame)
    {
        GameplayInput input;
        input.firePressed = true;
        input.aimWorldPosition = Vec2{pc.x + 500, pc.y};
        shooting.beginFrame(1.0F / 30);
        shooting.updateAim(input, pc, {1, 0}, saved->worldSize, 1.0F / 30);
        runtime.advance(input, 1.0F / 30, player, {40, 52}, false, shooting, walls);
    }
    EXPECT_TRUE(runtime.breached()) << "active=" << runtime.enemies().size();
    EXPECT_TRUE(std::any_of(runtime.state().breachedIds.begin(), runtime.state().breachedIds.end(),
        [&](auto id) { return std::find(saved->wavePlans.front().enemyIds.begin(),
            saved->wavePlans.front().enemyIds.end(), id) != saved->wavePlans.front().enemyIds.end(); }));
}

TEST(BaseDefenseRuntimeTest, WallTouchingInvaderCanResumeAndLeaveTheWall)
{
    auto saved = arena();
    ASSERT_TRUE(saved);
    auto &wave = saved->wavePlans.front();
    // Retain the old event's frozen health; the new-wave balance is not a
    // reason to rewrite existing actors or respawn defeated enemies on load.
    for (auto &oldWave : saved->wavePlans) oldWave.enemyMaxHealth = 100;
    saved->layoutHash = baseDefenseLayoutHash(*saved);
    const Vec2 start{wave.entry.x - 16, wave.entry.y - 24};
    const std::vector<BallisticBlocker> wall{
        {1, {{wave.entry.x + 16, wave.entry.y - 100}, {20, 200}}}};
    saved->spawnedEnemyCount = 1;
    saved->nextSpawnDelay = 10;
    saved->enemies.push_back(Enemy{start, {32, 48}, {}, 100, wave.enemyIds.front()}.checkpoint());
    nlohmann::json encoded = saved->enemies.front();
    saved->enemies.front() = encoded.get<EnemyRuntimeCheckpoint>();
    std::string reason;
    ASSERT_TRUE(validateBaseDefenseSnapshot(*saved, reason)) << reason;
    BaseDefenseRuntime runtime;
    ASSERT_TRUE(runtime.resume(*saved, wall));
    WorldShootingRuntime shooting;
    for (unsigned frame = 0; frame < 60; ++frame) {
        shooting.beginFrame(1.0F / 30);
        runtime.advance({}, 1.0F / 30, saved->playerPosition, {40, 52}, false, shooting, wall);
    }
    ASSERT_FALSE(runtime.enemies().empty());
    const auto &enemy = runtime.enemies().front();
    EXPECT_GT(std::hypot(enemy.position().x - start.x, enemy.position().y - start.y), 50);
    EXPECT_EQ(enemy.maxHealth(), 100);
    BaseDefenseRuntime restored;
    ASSERT_TRUE(restored.resume(runtime.checkpoint(shooting), wall));
}

TEST(BaseDefenseRuntimeTest, ResumeProducesSameWaveAndMovementContinuation)
{
    BaseWorld first, second;
    auto initial = first.prepareBaseDefenseSnapshot(seed(), publishedContentRegistry().enemyCombatDefinition(ordinaryInfectedDefinitionId()));
    ASSERT_TRUE(initial);
    ASSERT_TRUE(first.resumeBaseDefense(*initial));
    for (unsigned i = 0; i < 180; ++i)
        static_cast<void>(first.update({}, 1.0F / 30.0F));
    auto saved = first.baseDefenseCheckpoint();
    ASSERT_TRUE(saved);
    ASSERT_TRUE(second.resumeBaseDefense(*saved));
    for (unsigned i = 0; i < 180; ++i)
    {
        static_cast<void>(first.update({}, 1.0F / 30.0F));
        static_cast<void>(second.update({}, 1.0F / 30.0F));
        EXPECT_EQ(baseDefenseCheckpointHash(*first.baseDefenseCheckpoint()),
                  baseDefenseCheckpointHash(*second.baseDefenseCheckpoint()));
    }
}

TEST(BaseDefenseRuntimeTest, FullyBlockedSiteRejectsWithoutPartialWave)
{
    auto input = seed();
    input.siteDefinitionId = "regional_base_site.greyline_yard";
    input.layoutIdentity = "blocked-test";
    input.worldSize = {5000, 5000};
    input.safeCore = {{1800, 1800}, {1400, 1000}};
    input.playerPosition = {2300, 2300};
    const std::vector<BallisticBlocker> blockers{{1, {{0, 0}, {5000, 5000}}}};
    EXPECT_FALSE(BaseDefenseRuntime::prepare(input, blockers, publishedContentRegistry().enemyCombatDefinition(ordinaryInfectedDefinitionId())));
    EXPECT_TRUE(input.wavePlans.empty());
    EXPECT_EQ(input.spawnedEnemyCount, 0);
}

TEST(BaseDefenseRuntimeTest, OrdinaryPerimeterActorsAreNotSiegeTargetsOrRewards)
{
    BaseWorld world;
    HomePerimeterSiteSnapshot ordinary;
    ordinary.baseSiteDefinitionId = RegionalBaseSiteDefinitionId{world.siteDefinitionId()};
    ordinary.cycleIndex = 1;
    ordinary.enemies.push_back({1, {500, 500}, {500, 500}, {32, 48}, 100, 80});
    world.configureHomePerimeter(&ordinary);
    const auto before = world.perimeterEnemySnapshots();
    auto prepared = world.prepareBaseDefenseSnapshot(seed(), publishedContentRegistry().enemyCombatDefinition(ordinaryInfectedDefinitionId()));
    ASSERT_TRUE(prepared);
    ASSERT_TRUE(world.resumeBaseDefense(*prepared));
    for (unsigned i = 0; i < 180; ++i)
        static_cast<void>(world.update({}, 1.0F / 30));
    EXPECT_EQ(world.perimeterEnemySnapshots(), before);
    for (const auto &enemy : world.baseDefenseEnemies())
        EXPECT_NE(enemy.combatTargetId(), 1);
    world.clearBaseDefense();
    EXPECT_FALSE(world.baseDefenseActive());
    EXPECT_EQ(world.perimeterEnemySnapshots(), before);
}

TEST(BaseDefenseRuntimeTest, FacilityQueueGeometryIsFrozenAcrossResume)
{
    BaseWorld first;
    auto prepared = first.prepareBaseDefenseSnapshot(seed(), publishedContentRegistry().enemyCombatDefinition(ordinaryInfectedDefinitionId()));
    ASSERT_TRUE(prepared);
    ASSERT_TRUE(first.resumeBaseDefense(*prepared));
    const auto old = first.facilities().front().bounds;
    const std::vector<BaseFacilitySpatialOverride> changed{
        {BaseFacilityKind::Storage, {6000, 5000}, false}};
    first.configureSite(first.siteDefinitionId(), changed);
    EXPECT_EQ(first.facilities().front().bounds.position.x, old.position.x);
    for (unsigned i = 0; i < 20; ++i)
        static_cast<void>(first.update({}, 1.0F / 30));
    auto checkpoint = first.baseDefenseCheckpoint();
    ASSERT_TRUE(checkpoint);
    BaseWorld restored;
    restored.configureSite(first.siteDefinitionId(), changed);
    ASSERT_TRUE(restored.resumeBaseDefense(*checkpoint));
    EXPECT_EQ(baseDefenseCheckpointHash(*checkpoint),
              baseDefenseCheckpointHash(*restored.baseDefenseCheckpoint()));
    restored.clearBaseDefense();
    EXPECT_FALSE(restored.facilities().front().active);
}

TEST(BaseDefenseRuntimeTest, RealLogicalShotsCanCompleteAllThreeFiniteWaves)
{
    auto s = arena();
    ASSERT_TRUE(s);
    BaseDefenseRuntime runtime;
    ASSERT_TRUE(runtime.resume(*s, {}));
    WorldShootingRuntime shooting;
    const auto fixtureWeapon = *publishedContentRegistry().item(alpha_content::rifle).weaponUse;
    for (const auto &wave : s->wavePlans)
        ASSERT_EQ(wave.enemyMaxHealth, 12);
    ASSERT_EQ(fixtureWeapon.baseDamage, 4);
    auto handling = deriveWeaponHandling(fixtureWeapon);
    handling.minimumSpreadDegrees = 0;
    handling.maximumSpreadDegrees = 0;
    handling.recoilInitialSpeed = 0;
    shooting.configureWeapon(fixtureWeapon, handling, false);
    const Vec2 player = s->playerPosition, size{40, 52};
    const Vec2 origin{player.x + size.x / 2, player.y + size.y / 2};
    unsigned shots = 0;
    for (unsigned frame = 0; frame < 5000 && !runtime.completed() && !runtime.breached(); ++frame)
    {
        GameplayInput input;
        if (!runtime.enemies().empty())
        {
            const auto &enemy = runtime.enemies().front();
            const Vec2 target{enemy.position().x + enemy.size().x / 2,
                              enemy.position().y + enemy.size().y / 2};
            input.aimWorldPosition = target;
            input.aimMotionDelta = Vec2{target.x - shooting.aimWorldPosition().x,
                                        target.y - shooting.aimWorldPosition().y};
            input.firePressed = true;
        }
        shooting.beginFrame(0.02F);
        shooting.updateAim(input, origin, {1, 0}, s->worldSize, 0.02F);
        runtime.advance(input, 0.02F, player, size, false, shooting, {});
        if (shooting.shotFiredLastUpdate())
            ++shots;
    }
    ASSERT_TRUE(runtime.completed());
    EXPECT_FALSE(runtime.breached());
    EXPECT_EQ(runtime.state().breachedIds.size(), 0);
    EXPECT_EQ(runtime.state().killedIds.size(), runtime.state().spawnedEnemyCount);
    EXPECT_GE(shots, runtime.state().killedIds.size());
    EXPECT_EQ(runtime.state().currentWave, 2);
    std::string reason;
    EXPECT_TRUE(validateBaseDefenseSnapshot(runtime.checkpoint(shooting), reason)) << reason;
}

TEST(BaseDefenseRuntimeTest, AttackQuotaAndDamageProtectionResumeWithoutDuplicateHit)
{
    auto saved = arena();
    ASSERT_TRUE(saved);
    saved->elapsedSeconds = 24;
    saved->currentWave = 1;
    saved->spawnedEnemyCount = 16;
    const Vec2 player = saved->wavePlans[0].entry, size{40, 52};
    saved->playerPosition = player;
    std::size_t count = 0;
    for (const auto &wave : saved->wavePlans)
        for (auto id : wave.enemyIds)
        {
            if (count == 16)
                break;
            Enemy enemy{{player.x + 48 + static_cast<float>(count % 4) * 4,
                         player.y + static_cast<float>(count / 4) * 4},
                        {32, 48},
                        {},
                        wave.enemyMaxHealth,
                        id};
            saved->enemies.push_back(enemy.checkpoint());
            ++count;
        }
    BaseDefenseRuntime first;
    ASSERT_TRUE(first.resume(*saved, {}));
    WorldShootingRuntime shooting;
    double lastHit = -1, time = 0;
    unsigned hits = 0;
    for (unsigned frame = 0; frame < 300; ++frame)
    {
        time += 0.01;
        shooting.beginFrame(0.01F);
        first.advance({}, 0.01F, player, size, false, shooting, {});
        std::size_t attacking = 0;
        for (const auto &e : first.enemies())
            if (e.attackPhase() == EnemyAttackPhase::Windup ||
                e.attackPhase() == EnemyAttackPhase::Active ||
                e.attackPhase() == EnemyAttackPhase::Recovery)
                ++attacking;
        EXPECT_LE(attacking, 10);
        if (first.damageLastUpdate())
        {
            if (lastHit >= 0)
                EXPECT_GE(time - lastHit, 0.245);
            lastHit = time;
            ++hits;
            auto checkpoint = first.checkpoint(shooting);
            for (auto id : checkpoint.reservedAttackers)
                EXPECT_GE(id, 0x8000000000000001ULL);
            BaseDefenseRuntime second;
            ASSERT_TRUE(second.resume(checkpoint, {}));
            WorldShootingRuntime shooting2;
            ASSERT_TRUE(shooting2.restoreCheckpoint(checkpoint.shooting));
            shooting.beginFrame(0.01F);
            shooting2.beginFrame(0.01F);
            first.advance({}, 0.01F, player, size, false, shooting, {});
            second.advance({}, 0.01F, player, size, false, shooting2, {});
            time += 0.01;
            EXPECT_EQ(first.damageLastUpdate(), 0);
            EXPECT_EQ(second.damageLastUpdate(), 0);
            EXPECT_EQ(baseDefenseCheckpointHash(first.checkpoint(shooting)),
                      baseDefenseCheckpointHash(second.checkpoint(shooting2)));
        }
    }
    EXPECT_GE(hits, 2);
}

TEST(BaseDefenseRuntimeTest, BreachRetiresBeforeAttackAndStopsAtLimitWithoutErasingEarlierDamage)
{
    for (const bool earlierDamage : {false, true})
    {
        auto saved = arena();
        ASSERT_TRUE(saved);
        const auto &wave = saved->wavePlans.front();
        saved->elapsedSeconds = 1.0F;
        saved->spawnedEnemyCount = earlierDamage ? 8U : 7U;
        saved->nextSpawnDelay = 1.0F;
        saved->breachedIds.assign(wave.enemyIds.begin(), wave.enemyIds.begin() + 5);
        const Vec2 player{wave.target.x - 20.0F, wave.target.y - 26.0F};
        saved->playerPosition = player;
        for (std::uint32_t index = 5; index < saved->spawnedEnemyCount; ++index)
        {
            Enemy enemy{{wave.target.x - 16.0F, wave.target.y - 24.0F},
                        {32.0F, 48.0F}, {}, wave.enemyMaxHealth, wave.enemyIds[index]};
            ASSERT_TRUE(enemy.tryStartAttack(EnemyAttackType::Scratch, {1.0F, 0.0F}));
            const auto config = enemy.attackConfig();
            ASSERT_TRUE(config);
            static_cast<void>(enemy.update(config->windupDuration + 0.001F, 6000, 6000));
            ASSERT_EQ(enemy.attackPhase(), EnemyAttackPhase::Active);
            saved->enemies.push_back(enemy.checkpoint());
            // Both final actors reach the contact threshold in the same step.
            // An optional earlier actor is allowed to land its legal hit first.
            if (!earlierDamage || index > 5)
                saved->contacts.push_back({wave.enemyIds[index], 0.345F});
        }
        BaseDefenseRuntime runtime;
        ASSERT_TRUE(runtime.resume(*saved, {}));
        WorldShootingRuntime shooting;
        shooting.beginFrame(0.01F);
        runtime.advance({}, 0.01F, player, {40.0F, 52.0F}, false, shooting, {});
        EXPECT_TRUE(runtime.breached());
        EXPECT_EQ(runtime.state().breachedIds.size(), 6U);
        EXPECT_EQ(runtime.damageLastUpdate(), earlierDamage ? 12 : 0);
        EXPECT_EQ(runtime.enemies().size(), earlierDamage ? 2U : 1U);
        std::string reason;
        EXPECT_TRUE(validateBaseDefenseSnapshot(runtime.checkpoint(shooting), reason)) << reason;
    }
}

TEST(BaseDefenseRuntimeTest, BreachBeforeShotCannotAlsoKillAndTerminalFrameCannotFire)
{
    for (bool terminal : {false, true})
    {
        auto saved = arena();
        ASSERT_TRUE(saved);
        const auto &wave = saved->wavePlans.front();
        const auto alreadyBreached = terminal ? 5U : 0U;
        saved->elapsedSeconds = 1;
        saved->spawnedEnemyCount = alreadyBreached + 1;
        saved->nextSpawnDelay = 1000;
        saved->breachedIds.assign(wave.enemyIds.begin(), wave.enemyIds.begin() + alreadyBreached);
        const auto id = wave.enemyIds[alreadyBreached];
        Enemy enemy{{wave.target.x - 16, wave.target.y - 24}, {32, 48}, {}, 12, id};
        saved->enemies.push_back(enemy.checkpoint());
        saved->contacts.push_back({id, 0.345F});
        BaseDefenseRuntime runtime;
        ASSERT_TRUE(runtime.resume(*saved, {}));
        WorldShootingRuntime shooting;
        auto shotState = shooting.checkpoint();
        LogicalFlightCheckpoint shot;
        shot.id = shotState.nextShotId++;
        shot.origin = shot.position = checkpointPoint(wave.target);
        shot.direction = {1, 0};
        shot.impact = {wave.target.x + 100, wave.target.y};
        shot.speed = 6000;
        shot.extent = 1;
        shot.maximumDistance = 100;
        shot.damage = 100;
        shot.tracerLifetime = 0.05F;
        shotState.flights.push_back(shot);
        ASSERT_TRUE(shooting.restoreCheckpoint(shotState));
        GameplayInput fire;
        fire.fireJustPressed = true;
        shooting.beginFrame(0.01F);
        runtime.advance(fire, 0.01F, saved->playerPosition, {40, 52}, false, shooting, {});
        EXPECT_EQ(runtime.breached(), terminal);
        EXPECT_EQ(runtime.state().breachedIds.size(), alreadyBreached + 1);
        EXPECT_TRUE(runtime.state().killedIds.empty());
        EXPECT_TRUE(runtime.enemies().empty());
        EXPECT_TRUE(shooting.hitResultsLastUpdate().empty());
        EXPECT_EQ(shooting.shotFiredLastUpdate(), !terminal);
        if (terminal)
        {
            EXPECT_EQ(shooting.checkpoint().nextShotId, shotState.nextShotId);
            EXPECT_EQ(shooting.checkpoint().flights, shotState.flights);
        }
        const auto checkpoint = runtime.checkpoint(shooting);
        std::string reason;
        ASSERT_TRUE(validateBaseDefenseSnapshot(checkpoint, reason)) << reason;
        BaseDefenseRuntime resumed;
        WorldShootingRuntime resumedShooting;
        ASSERT_TRUE(resumed.resume(checkpoint, {}));
        ASSERT_TRUE(resumedShooting.restoreCheckpoint(checkpoint.shooting));
        shooting.beginFrame(0.01F);
        resumedShooting.beginFrame(0.01F);
        runtime.advance({}, 0.01F, saved->playerPosition, {40, 52}, false, shooting, {});
        resumed.advance({}, 0.01F, saved->playerPosition, {40, 52}, false, resumedShooting, {});
        EXPECT_EQ(baseDefenseCheckpointHash(runtime.checkpoint(shooting)),
                  baseDefenseCheckpointHash(resumed.checkpoint(resumedShooting)));
        EXPECT_EQ(runtime.state().breachedIds.size(), alreadyBreached + 1);
        EXPECT_TRUE(runtime.state().killedIds.empty());
    }
}
