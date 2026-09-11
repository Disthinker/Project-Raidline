#include "alpha_content_ids.h"
#include "base_defense_positions.h"
#include "base_defense_runtime.h"
#include "base_defense_serialization.h"
#include "base_world.h"
#include "fortification_runtime.h"
#include "hit_resolution.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <gtest/gtest.h>
#include <iostream>
#include <nlohmann/json.hpp>

namespace
{
FortificationSnapshot wood(std::uint64_t id = 1, std::uint32_t hp = 120)
{
    return {
        {id},
        kWoodBarricadeDefinition,
        {RegionalBaseSiteDefinitionId{"regional_base_site.greyline_yard"}, "", DefenseSide::West},
        {{150, 90}, {48, 160}},
        120,
        hp,
        hp};
}
Enemy scratch(std::uint64_t id = 1)
{
    Enemy e{{100, 100}, {32, 48}, {}, 12, id};
    e.hearTarget({150, 124});
    EXPECT_TRUE(e.tryStartStructureScratch({150, 124}));
    static_cast<void>(e.update(0.19F, 6000, 6000));
    EXPECT_TRUE(e.hasAttackHitOpportunity());
    return e;
}
const EnemyCombatDefinition &infected()
{
    return publishedContentRegistry().enemyCombatDefinition(ordinaryInfectedDefinitionId());
}
BaseDefenseSnapshot seed()
{
    BaseDefenseSnapshot s;
    s.eventId = "fortified-test";
    s.siegeSequence = 1;
    s.seed = 12345;
    s.siteDefinitionId = "regional_base_site.greyline_yard";
    s.layoutIdentity = "fortified-arena";
    s.worldSize = {6000, 6000};
    s.safeCore = {{2300, 2300}, {1200, 1000}};
    s.playerPosition = {2800, 2700};
    s.frozenMoraleTier = 1;
    return s;
}
std::optional<BaseDefenseSnapshot> fortified()
{
    auto s = seed();
    const auto legacy = BaseDefenseRuntime::prepare(s, {}, infected());
    if (!legacy)
        return {};
    const auto target = legacy->wavePlans.front().target;
    auto f = wood();
    Vec2 middle = target;
    if (target.x < s.safeCore.position.x)
    {
        middle.x -= 200;
        f.slot.side = DefenseSide::West;
    }
    else if (target.x > s.safeCore.position.x + s.safeCore.size.x)
    {
        middle.x += 200;
        f.slot.side = DefenseSide::East;
    }
    else if (target.y < s.safeCore.position.y)
    {
        middle.y -= 200;
        f.slot.side = DefenseSide::North;
        f.footprint.size = {160, 48};
    }
    else
    {
        middle.y += 200;
        f.slot.side = DefenseSide::South;
        f.footprint.size = {160, 48};
    }
    f.footprint.position = {middle.x - f.footprint.size.x / 2, middle.y - f.footprint.size.y / 2};
    s.rulesVersion = kFortifiedBaseDefenseRulesVersion;
    s.fortifications = {f};
    return BaseDefenseRuntime::prepare(s, {}, infected());
}
void tick(BaseDefenseRuntime &r, WorldShootingRuntime &shooting, float dt = 1.0F / 30)
{
    shooting.beginFrame(dt);
    r.advance({}, dt, r.state().playerPosition, {40, 52}, false, shooting, {});
}
} // namespace

TEST(FortificationRuntimeTest, ScratchConsumedOnceWithOneDisableAndNoEnemyMutation)
{
    const auto geometry = RaidSpaceBlockerIndex::build({6000, 6000}, {}, 320);
    ASSERT_TRUE(geometry);
    FortificationRuntime r;
    const std::vector states{wood(17, 24)};
    ASSERT_TRUE(r.restore(states, 0));
    auto first = scratch(41), second = scratch(42), late = scratch(43);
    const auto position = first.position();
    EXPECT_TRUE(r.consumeScratch(first, {17}, *geometry));
    EXPECT_FALSE(r.consumeScratch(first, {17}, *geometry));
    EXPECT_TRUE(r.consumeScratch(second, {17}, *geometry));
    EXPECT_FALSE(r.consumeScratch(late, {17}, *geometry));
    EXPECT_EQ(r.find({17})->durability, 0);
    ASSERT_EQ(r.damageFacts().size(), 2);
    ASSERT_EQ(r.disabledFacts().size(), 1);
    EXPECT_EQ(r.disabledFacts()[0].source, 42);
    EXPECT_EQ(r.geometryRevision(), 1);
    EXPECT_EQ(first.combatTargetId(), 41);
    EXPECT_EQ(first.health(), 12);
    EXPECT_EQ(first.position().x, position.x);
    EXPECT_FALSE(first.hasAttackHitOpportunity());
    EXPECT_TRUE(late.hasAttackHitOpportunity());
    EXPECT_TRUE(r.clear({{150, 90}, {48, 160}}));
    EXPECT_TRUE(r.lineOfSight({100, 124}, {230, 124}));
}

TEST(FortificationRuntimeTest, DeadUnknownIdleGrabAndOccludedAttacksCannotDamage)
{
    FortificationRuntime r;
    ASSERT_TRUE(r.restore(std::vector{wood()}, 0));
    auto geometry = RaidSpaceBlockerIndex::build({6000, 6000}, {}, 320);
    auto dead = scratch();
    static_cast<void>(dead.takeDamage(100));
    EXPECT_FALSE(r.consumeScratch(dead, {1}, *geometry));
    auto unknown = scratch();
    EXPECT_FALSE(r.consumeScratch(unknown, {99}, *geometry));
    Enemy grab{{100, 100}, {32, 48}, {}, 12, 2};
    ASSERT_TRUE(grab.tryStartAttack(EnemyAttackType::Grab, {1, 0}));
    static_cast<void>(grab.update(0.56F, 6000, 6000));
    EXPECT_FALSE(r.consumeScratch(grab, {1}, *geometry));
    auto blocked = scratch(3);
    const std::vector<BallisticBlocker> walls{{9, {{134, 90}, {10, 160}}}};
    geometry = RaidSpaceBlockerIndex::build({6000, 6000}, walls, 320);
    EXPECT_FALSE(r.consumeScratch(blocked, {1}, *geometry));
    EXPECT_TRUE(blocked.hasAttackHitOpportunity());
    EXPECT_EQ(r.find({1})->durability, 120);
    EXPECT_TRUE(r.damageFacts().empty());
}

TEST(FortificationRuntimeTest, SurfaceLosIgnoresOnlyAttackedWoodAndSelectsOnlyNearObstruction)
{
    auto first = wood(11), other = wood(12);
    other.slot.side = DefenseSide::East;
    other.footprint = {{134, 90}, {10, 160}};
    FortificationRuntime r;
    ASSERT_TRUE(r.restore(std::vector{first, other}, 0));
    auto e = scratch();
    const auto geometry = RaidSpaceBlockerIndex::build({6000, 6000}, {}, 320);
    EXPECT_FALSE(r.consumeScratch(e, {11}, *geometry));
    ASSERT_NE(r.obstructing({116, 124}, {250, 124}), nullptr);
    EXPECT_EQ(r.obstructing({116, 124}, {250, 124})->id.value, 12);
    EXPECT_EQ(r.obstructing({116, 124}, {116, 500}), nullptr);
    EXPECT_EQ(r.obstructing({600, 124}, {100, 124}), nullptr);
}

TEST(FortificationRuntimeTest, HorizontalVerticalAndBallisticBlockingEndsAtZero)
{
    FortificationRuntime r;
    ASSERT_TRUE(r.restore(std::vector{wood(1, 12)}, 0));
    EXPECT_FLOAT_EQ(r.resolveMovement({{100, 100}, {32, 48}}, {170, 100}).x, 118);
    EXPECT_FLOAT_EQ(r.resolveMovement({{155, 20}, {32, 48}}, {155, 120}).y, 42);
    EXPECT_FALSE(r.lineOfSight({116, 124}, {230, 124}));
    auto e = scratch();
    const auto geometry = RaidSpaceBlockerIndex::build({6000, 6000}, {}, 320);
    ASSERT_TRUE(r.consumeScratch(e, {1}, *geometry));
    EXPECT_FLOAT_EQ(r.resolveMovement({{100, 100}, {32, 48}}, {170, 100}).x, 170);
    EXPECT_FLOAT_EQ(r.resolveMovement({{155, 20}, {32, 48}}, {155, 120}).y, 120);
    FortificationRuntime restored;
    ASSERT_TRUE(restored.restore(r.snapshots(), r.geometryRevision()));
    EXPECT_TRUE(restored.lineOfSight({116, 124}, {230, 124}));
    EXPECT_TRUE(restored.disabledFacts().empty());
    EXPECT_FALSE(restored.consumeScratch(e, {1}, *geometry));
}

TEST(FortificationRuntimeTest, MalformedRestoreIsAtomicAndStableIdsSurviveFirstMiddleLastDisable)
{
    FortificationRuntime r;
    std::vector<FortificationSnapshot> states;
    for (unsigned i = 0; i < 4; ++i)
    {
        auto f = wood(10 + i, 12);
        f.slot.side = static_cast<DefenseSide>(i);
        f.footprint.position.y += 200 * i;
        states.push_back(f);
    }
    ASSERT_TRUE(r.restore(states, 0));
    auto invalid = states;
    invalid[1].id = invalid[0].id;
    EXPECT_FALSE(r.restore(invalid, 0));
    invalid = states;
    invalid[0].durability = 121;
    EXPECT_FALSE(r.restore(invalid, 0));
    EXPECT_FALSE(r.restore(states, 1));
    const auto geometry = RaidSpaceBlockerIndex::build({6000, 6000}, {}, 320);
    for (unsigned i : {0U, 2U, 3U})
    {
        auto e = scratch(10 + i);
        ASSERT_TRUE(e.setPosition({100, 100.0F + 200 * i}));
        EXPECT_TRUE(r.consumeScratch(e, {10 + i}, *geometry));
        EXPECT_EQ(r.find({11})->durability, 12);
        EXPECT_FLOAT_EQ(r.find({11})->footprint.position.y, states[1].footprint.position.y);
    }
    EXPECT_EQ(r.snapshots().size(), 4);
    EXPECT_EQ(r.geometryRevision(), 3);
}

TEST(FortificationRuntimeTest, StructureScratchUsesExistingCooldownAndNeverArmsBite)
{
    auto e = scratch();
    EXPECT_EQ(e.attackType(), EnemyAttackType::Scratch);
    EXPECT_FALSE(e.tryStartStructureScratch({150, 124}));
    for (unsigned i = 0; i < 150; ++i)
    {
        EnemyTacticalDirective directive;
        directive.role = EnemyTacticalRole::Engage;
        static_cast<void>(e.updateTowardsTarget({150, 124}, directive, 0.01F, 6000, 6000));
        EXPECT_NE(e.attackType(), EnemyAttackType::Bite);
    }
    EXPECT_TRUE(e.hasStructureScratchOpportunity({150, 124}));
}

TEST(FortificationDefenseTest, RealWavesDamageWoodOpenGapAndContinueWithoutRebuild)
{
    auto s = fortified();
    ASSERT_TRUE(s);
    BaseDefenseRuntime r;
    ASSERT_TRUE(r.resume(*s, {}));
    WorldShootingRuntime shooting;
    const auto hash = s->layoutHash;
    unsigned damage = 0, disabled = 0;
    for (unsigned frame = 0; frame < 7200 && !r.breached(); ++frame)
    {
        tick(r, shooting);
        EXPECT_EQ(r.state().layoutHash, hash);
        EXPECT_LE(r.metrics().navigationQueries, r.metrics().substeps);
        damage += static_cast<unsigned>(r.fortifications().damageFacts().size());
        disabled += static_cast<unsigned>(r.fortifications().disabledFacts().size());
        EXPECT_EQ(r.damageLastUpdate(), 0);
        for (const auto &e : r.enemies())
            EXPECT_TRUE(r.fortifications().clear(e.bounds()));
    }
    EXPECT_GT(damage, 0);
    EXPECT_EQ(disabled, 1);
    EXPECT_EQ(r.fortifications().find({1})->durability, 0);
    EXPECT_TRUE(r.breached());
    EXPECT_TRUE(r.state().killedIds.empty());
    std::string message;
    EXPECT_TRUE(validateBaseDefenseSnapshot(r.checkpoint(shooting), message)) << message;
}

TEST(FortificationDefenseTest, FrozenRoundTripContinuesIdenticallyDuringWindupDamageAndAfterDisable)
{
    auto s = fortified();
    ASSERT_TRUE(s);
    BaseDefenseRuntime first;
    ASSERT_TRUE(first.resume(*s, {}));
    WorldShootingRuntime shooting;
    bool windup = false, damage = false, disabled = false;
    for (unsigned frame = 0; frame < 6000 && !first.breached(); ++frame)
    {
        tick(first, shooting);
        const auto hp = first.fortifications().find({1})->durability;
        const bool checkWindup = !windup && !first.state().fortificationAttacks.empty();
        const bool checkDamage = !damage && hp > 0 && hp < 120;
        const bool checkDisabled = !disabled && hp == 0;
        if (!checkWindup && !checkDamage && !checkDisabled)
            continue;
        windup |= checkWindup;
        damage |= checkDamage;
        disabled |= checkDisabled;
        const auto before = first.checkpoint(shooting);
        const auto encoded = baseDefenseSnapshotJson(before).dump();
        const auto restored = parseBaseDefenseSnapshotJson(nlohmann::json::parse(encoded));
        ASSERT_EQ(baseDefenseCheckpointHash(before), baseDefenseCheckpointHash(restored));
        BaseDefenseRuntime second;
        ASSERT_TRUE(second.resume(restored, {}));
        WorldShootingRuntime secondShooting;
        ASSERT_TRUE(secondShooting.restoreCheckpoint(restored.shooting));
        for (unsigned i = 0; i < 12; ++i)
        {
            tick(first, shooting);
            tick(second, secondShooting);
            ASSERT_EQ(baseDefenseCheckpointHash(first.checkpoint(shooting)),
                      baseDefenseCheckpointHash(second.checkpoint(secondShooting)));
        }
    }
    EXPECT_TRUE(windup);
    EXPECT_TRUE(damage);
    EXPECT_TRUE(disabled);
}

TEST(FortificationDefenseTest, CorruptGeometryRevisionBindingOrUnsignedJsonIsRejected)
{
    auto s = fortified();
    ASSERT_TRUE(s);
    auto json = baseDefenseSnapshotJson(*s);
    for (const auto &value : {nlohmann::json(-1), nlohmann::json(1.5), nlohmann::json(UINT64_MAX)})
    {
        auto bad = json;
        bad["fortifications"][0]["durability"] = value;
        EXPECT_THROW(static_cast<void>(parseBaseDefenseSnapshotJson(bad)), std::exception);
    }
    auto bad = json;
    bad.erase("fortifications");
    EXPECT_THROW(static_cast<void>(parseBaseDefenseSnapshotJson(bad)), std::exception);
    bad = json;
    bad["fortification_geometry_revision"] = 1;
    EXPECT_THROW(static_cast<void>(parseBaseDefenseSnapshotJson(bad)), std::exception);
    bad = json;
    bad["fortifications"][0]["footprint"][0][0] = 30;
    EXPECT_THROW(static_cast<void>(parseBaseDefenseSnapshotJson(bad)), std::exception);
    bad = json;
    bad["fortification_attacks"].push_back({{"enemy", 1}, {"target", 1}});
    EXPECT_THROW(static_cast<void>(parseBaseDefenseSnapshotJson(bad)), std::exception);
    bad = json;
    bad["rules_version"] = 1;
    EXPECT_THROW(static_cast<void>(parseBaseDefenseSnapshotJson(bad)), std::exception);
}

TEST(FortificationDefenseTest, LegacySnapshotDoesNotAcquireWoodOrChangeEncoding)
{
    const auto s = BaseDefenseRuntime::prepare(seed(), {}, infected());
    ASSERT_TRUE(s);
    const auto json = baseDefenseSnapshotJson(*s);
    EXPECT_FALSE(json.contains("fortifications"));
    const auto restored = parseBaseDefenseSnapshotJson(json);
    EXPECT_EQ(baseDefenseCheckpointHash(restored), baseDefenseCheckpointHash(*s));
    EXPECT_EQ(baseDefenseSnapshotJson(restored), json);
    auto bad = *s;
    bad.fortifications = {wood()};
    bad.layoutHash = baseDefenseLayoutHash(bad);
    BaseDefenseRuntime r;
    EXPECT_FALSE(r.resume(bad, {}));
}

TEST(FortificationDefenseTest, CancelledWoodScratchCannotTurnIntoPlayerHitAfterRestore)
{
    auto prepared = fortified();
    ASSERT_TRUE(prepared);
    auto s = *prepared;
    const auto &f = s.fortifications.front();
    const auto r = f.footprint;
    const Vec2 center{r.position.x - 34, r.position.y + r.size.y / 2};
    Enemy e{{center.x - 16, center.y - 24},
            {32, 48},
            {},
            s.wavePlans.front().enemyMaxHealth,
            s.wavePlans.front().enemyIds.front()};
    ASSERT_TRUE(e.tryStartStructureScratch({r.position.x, center.y}));
    static_cast<void>(e.update(0.19F, 6000, 6000));
    s.enemies = {e.checkpoint()};
    s.spawnedEnemyCount = 1;
    s.nextSpawnDelay = 1000;
    s.fortificationAttacks = {{e.combatTargetId(), f.id}};
    s.playerPosition = {r.position.x - 10, center.y - 26};
    s.fortifications[0].durability = 0;
    s.fortificationGeometryRevision = 1;
    s.corridors.push_back(
        {{r.position.x - 100, r.position.y - 50}, {r.size.x + 200, r.size.y + 100}});
    s.layoutHash = baseDefenseLayoutHash(s);
    s = parseBaseDefenseSnapshotJson(baseDefenseSnapshotJson(s));
    BaseDefenseRuntime bound, control;
    ASSERT_TRUE(bound.resume(s, {}));
    s.fortificationAttacks.clear();
    ASSERT_TRUE(control.resume(s, {}));
    WorldShootingRuntime a, b;
    tick(bound, a, 0.01F);
    tick(control, b, 0.01F);
    EXPECT_EQ(bound.damageLastUpdate(), 0);
    EXPECT_GT(control.damageLastUpdate(), 0); // same real hitbox can touch the player
    EXPECT_TRUE(bound.fortifications().damageFacts().empty());
}

TEST(FortificationDefenseTest, RealBaseWorldPlayerAndShotsRespectLiveAndDestroyedWood)
{
    BaseWorld world;
    const auto *definition = publishedContentRegistry().findFortification(kWoodBarricadeDefinition);
    ASSERT_NE(definition, nullptr);
    const auto candidates = baseDefensePositionCandidates(world.layout(), "", *definition);
    const auto chosen = std::find_if(candidates.begin(), candidates.end(),
                                     [](const auto &c) { return c.available; });
    ASSERT_NE(chosen, candidates.end());
    auto f = wood();
    f.slot = chosen->key;
    f.footprint = {chosen->footprint.position, chosen->footprint.size};
    auto input = seed();
    input.rulesVersion = kFortifiedBaseDefenseRulesVersion;
    input.fortifications = {f};
    auto prepared = world.prepareBaseDefenseSnapshot(input, infected());
    ASSERT_TRUE(prepared);
    const auto r = f.footprint;
    // Test the real BaseWorld collision path, not just the structure helper.
    prepared->playerPosition = {r.position.x - 42, r.position.y + r.size.y / 2 - 26};
    prepared->nextSpawnDelay = 1000;
    ASSERT_TRUE(world.resumeBaseDefense(*prepared));
    BaseInput move;
    move.moveRight = true;
    for (unsigned i = 0; i < 20; ++i)
        static_cast<void>(world.update(move, 0.1F));
    EXPECT_LE(world.playerPosition().x + world.playerSize().x, r.position.x + 0.01F);
    auto dead = *prepared;
    dead.fortifications[0].durability = 0;
    dead.fortificationGeometryRevision = 1;
    ASSERT_TRUE(world.resumeBaseDefense(dead));
    for (unsigned i = 0; i < 4; ++i)
        static_cast<void>(world.update(move, 0.1F));
    EXPECT_GT(world.playerPosition().x + world.playerSize().x, r.position.x + 1);

    BaseDefenseRuntime solid, broken;
    std::vector<BallisticBlocker> staticGeometry;
    for (const auto &b : prepared->movementBlockers)
        staticGeometry.push_back({staticGeometry.size() + 1, b});
    ASSERT_TRUE(solid.resume(*prepared, staticGeometry));
    ASSERT_TRUE(broken.resume(dead, staticGeometry));
    const Vec2 origin{r.position.x - 60, r.position.y + r.size.y / 2};
    const Vec2 end{r.position.x + r.size.x + 60, origin.y};
    EnemyRoster<> actors;
    const std::vector<ShotCollisionCandidate> shots{{1, origin, end, 1, 12}};
    const auto blocked = resolveShotHits(shots, actors, solid.worldBlockers());
    ASSERT_EQ(blocked.hits.size(), 1);
    EXPECT_EQ(blocked.hits[0].targetKind, HitTargetKind::Obstacle);
    EXPECT_TRUE(resolveShotHits(shots, actors, broken.worldBlockers()).hits.empty());
    EXPECT_EQ(solid.fortifications().find({1})->durability, 120); // own shots do not damage wood
}

TEST(FortificationDefenseTest, FourDisablesSixteenEnemiesThousandBlockersStayBelowSubstepBudget)
{
    auto prepared = fortified();
    ASSERT_TRUE(prepared);
    auto s = *prepared;
    // A legal broad test corridor isolates worst-case simultaneous destruction
    // from route sampling. The real prepare/route policy is covered above.
    s.corridors = {{{0, 0}, {2300, 6000}},
                   {{3500, 0}, {2500, 6000}},
                   {{2300, 0}, {1200, 2300}},
                   {{2300, 3300}, {1200, 2700}}};
    s.fortifications.clear();
    const std::array<Rect, 4> footprints{{{{2050, 2620}, {48, 160}},
                                          {{3700, 2620}, {48, 160}},
                                          {{2700, 2050}, {160, 48}},
                                          {{2700, 3500}, {160, 48}}}};
    for (unsigned i = 0; i < 4; ++i)
    {
        auto f = wood(i + 1, 12);
        f.slot.side = static_cast<DefenseSide>(i);
        f.footprint = footprints[i];
        s.fortifications.push_back(f);
    }
    std::vector<BallisticBlocker> blockers;
    for (unsigned i = 0; i < 1000; ++i)
    {
        const Rect r{{20.0F + (i % 40) * 45, 20.0F + (i / 40) * 45}, {24, 24}};
        s.movementBlockers.push_back(r);
        blockers.push_back({i + 1, r});
    }
    std::vector<std::uint64_t> ids;
    for (const auto &w : s.wavePlans)
        ids.insert(ids.end(), w.enemyIds.begin(), w.enemyIds.end());
    for (unsigned i = 0; i < 16; ++i)
    {
        const auto &f = s.fortifications[i % 4];
        const auto r = f.footprint;
        const unsigned row = i / 4;
        const float gap = row < 2 ? 34.0F : 400.0F;
        const float lateral = 30 + row * 25.0F;
        Vec2 center;
        if (i % 4 == 0)
            center = {r.position.x - gap, r.position.y + lateral};
        else if (i % 4 == 1)
            center = {r.position.x + r.size.x + gap, r.position.y + lateral};
        else if (i % 4 == 2)
            center = {r.position.x + lateral, r.position.y - gap};
        else
            center = {r.position.x + lateral, r.position.y + r.size.y + gap};
        Enemy e{{center.x - 16, center.y - 24},
                {32, 48},
                {},
                s.wavePlans.front().enemyMaxHealth,
                ids[i]};
        if (row < 2)
        {
            ASSERT_TRUE(e.tryStartStructureScratch(FortificationRuntime::surfacePoint(r, center)));
            static_cast<void>(e.update(0.19F, 6000, 6000));
            s.fortificationAttacks.push_back({ids[i], f.id});
        }
        s.enemies.push_back(e.checkpoint());
    }
    s.spawnedEnemyCount = 16;
    s.elapsedSeconds = 24;
    s.currentWave = 1;
    s.nextSpawnDelay = 1000;
    s.layoutHash = baseDefenseLayoutHash(s);
    double maximum = 0;
    for (unsigned run = 0; run < 8; ++run)
    {
        BaseDefenseRuntime runtime;
        ASSERT_TRUE(runtime.resume(s, blockers));
        std::vector<const Enemy *> addresses;
        for (const auto &e : runtime.enemies())
            addresses.push_back(&e);
        WorldShootingRuntime shooting;
        tick(runtime, shooting, 0.01F);
        maximum = std::max(maximum, runtime.metrics().updateMilliseconds);
        EXPECT_EQ(runtime.metrics().fortificationsDisabled, 4);
        EXPECT_EQ(runtime.metrics().dynamicGeometryBatches, 1);
        EXPECT_EQ(runtime.fortifications().geometryRevision(), 4);
        EXPECT_EQ(runtime.enemies().size(), 16);
        EXPECT_TRUE(runtime.state().killedIds.empty());
        EXPECT_TRUE(runtime.state().breachedIds.empty());
        for (unsigned i = 0; i < 16; ++i)
        {
            EXPECT_EQ(&runtime.enemies()[i], addresses[i]);
            EXPECT_EQ(runtime.enemies()[i].combatTargetId(), ids[i]);
            EXPECT_FLOAT_EQ(runtime.enemies()[i].position().x, s.enemies[i].position[0]);
            EXPECT_FLOAT_EQ(runtime.enemies()[i].position().y, s.enemies[i].position[1]);
        }
        BaseDefenseRuntime resumed;
        ASSERT_TRUE(resumed.resume(runtime.checkpoint(shooting), blockers));
        EXPECT_EQ(resumed.worldBlockers().size(), blockers.size());
        EXPECT_TRUE(resumed.fortifications().disabledFacts().empty());
    }
    std::cout << "Four disables / 16 actors / 1000 blockers: max update " << maximum << " ms\n";
    EXPECT_LT(maximum, 25.0);
}
