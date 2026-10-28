#include "enemy.h"
#include "enemy_variant.h"
#include "game_session.h"
#include "gameplay_world.h"
#include <gtest/gtest.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <memory>
#include <stdexcept>

// Keep this accessor identical in test_runner_infected_performance.cpp. It
// stages inputs only; update, shooting, observation and retirement remain real.
struct RunnerInfectedTestAccess
{
    static ProfileState &profile(GameSession &session) { return session.profile_; }
    static void betweenRaids(GameSession &session) { session.state_ = GameSessionState::BetweenRaids; }
    static void replaceWorld(GameSession &session, RaidWorldConfig config)
    { session.world_ = std::make_unique<GameplayWorld>(std::move(config)); }
    static auto &actors(GameplayWorld &world) { return world.activeEnemies(); }
    static WorldShootingRuntime &shooting(GameplayWorld &world) { return world.shooting_; }
};

namespace
{
Enemy runner(Vec2 position = {100, 100}, CombatTargetId id = 41)
{
    return Enemy{position, {50, 50}, {}, 8, id, std::nullopt, runnerEnemyVariantSnapshot()};
}
EnemyTacticalDirective directive(EnemyTacticalRole role, bool attack = false)
{ return {role, attack, {}, 1}; }
void pursue(Enemy &enemy, EnemyTacticalRole role = EnemyTacticalRole::Pressure,
    bool visible = true, float seconds = 0.1F)
{
    static_cast<void>(enemy.updateTowardsTarget({400, 125}, directive(role), seconds,
        2000, 1200, visible));
}
bool overlaps(Rect a, Rect b)
{
    return a.position.x < b.position.x + b.size.x && a.position.x + a.size.x > b.position.x &&
        a.position.y < b.position.y + b.size.y && a.position.y + a.size.y > b.position.y;
}
RaidWorldConfig arena()
{
    RaidWorldConfig c;
    c.worldSize = {6000, 3000};
    c.playerSpawn = {100, 100};
    c.extractionPoint = {{5600, 2600}, {100, 100}};
    c.playerMaximumHealth = c.playerCurrentHealth = 10000;
    c.lightConeObservationEnabled = true;
    return c;
}
EnemySpawn spawn(Vec2 position, bool fast = true)
{
    EnemySpawn value{position, {50, 50}, fast ? 8 : 12};
    if (fast) value.variant = runnerEnemyVariantSnapshot();
    return value;
}
void queueHit(GameplayWorld &world, CombatTargetId id, int damage)
{
    const auto *enemy = RunnerInfectedTestAccess::actors(world).find(id);
    if (!enemy) throw std::runtime_error("missing runner target");
    const Vec2 center{enemy->position().x + enemy->size().x / 2,
        enemy->position().y + enemy->size().y / 2};
    auto state = RunnerInfectedTestAccess::shooting(world).checkpoint();
    LogicalFlightCheckpoint flight;
    flight.id = state.nextShotId++;
    flight.origin = flight.position = checkpointPoint(center);
    flight.direction = {1, 0};
    flight.impact = {center.x + 100, center.y};
    flight.speed = 6000; flight.extent = 1; flight.maximumDistance = 100;
    flight.damage = damage; flight.tracerLifetime = 0.05F;
    state.flights.push_back(flight);
    if (!RunnerInfectedTestAccess::shooting(world).restoreCheckpoint(state))
        throw std::runtime_error("invalid runner test shot");
}
void expectSameAttackConfig(const EnemyAttackConfig &a, const EnemyAttackConfig &b)
{
    EXPECT_FLOAT_EQ(a.windupDuration, b.windupDuration);
    EXPECT_FLOAT_EQ(a.activeDuration, b.activeDuration);
    EXPECT_FLOAT_EQ(a.recoveryDuration, b.recoveryDuration);
    EXPECT_FLOAT_EQ(a.missRecoveryDuration, b.missRecoveryDuration);
    EXPECT_FLOAT_EQ(a.lungeDistance, b.lungeDistance);
    EXPECT_FLOAT_EQ(a.hitboxSize.x, b.hitboxSize.x);
    EXPECT_FLOAT_EQ(a.hitboxSize.y, b.hitboxSize.y);
    EXPECT_FLOAT_EQ(a.hitboxForwardOffset, b.hitboxForwardOffset);
    EXPECT_EQ(a.damage, b.damage);
    EXPECT_FLOAT_EQ(a.controlDuration, b.controlDuration);
}
}

TEST(RunnerInfectedRuntimeTest, VisiblePressureWithoutAttackSlotMovesAt270)
{
    auto enemy = runner();
    pursue(enemy);
    EXPECT_EQ(enemy.awarenessState(), EnemyAwarenessState::Alerted);
    EXPECT_EQ(enemy.tacticalRole(), EnemyTacticalRole::Pressure);
    EXPECT_EQ(enemy.attackPhase(), EnemyAttackPhase::Idle);
    EXPECT_FLOAT_EQ(enemy.movementSpeed(), 270);
    EXPECT_NEAR(enemy.position().x, 127, 0.001F);
    EXPECT_EQ(enemy.health(), 8);
    EXPECT_FALSE(enemy.torsoArmor());
}

TEST(RunnerInfectedRuntimeTest, EngageWithoutPermissionPursuesButCannotStartAttack)
{
    auto enemy = runner();
    pursue(enemy, EnemyTacticalRole::Engage);
    EXPECT_FLOAT_EQ(enemy.movementSpeed(), 270);
    EXPECT_EQ(enemy.attackPhase(), EnemyAttackPhase::Idle);
    ASSERT_TRUE(enemy.setPosition({340, 100}));
    for (int n = 0; n < 30; ++n) pursue(enemy, EnemyTacticalRole::Engage, true, 0.01F);
    EXPECT_EQ(enemy.attackPhase(), EnemyAttackPhase::Idle);
    EXPECT_FALSE(enemy.attackType());
}

TEST(RunnerInfectedRuntimeTest, LossOfCurrentSightAndMemoryNavigationReturnTo90)
{
    auto enemy = runner();
    pursue(enemy);
    ASSERT_FLOAT_EQ(enemy.movementSpeed(), 270);
    pursue(enemy, EnemyTacticalRole::Pressure, false);
    EXPECT_EQ(enemy.awarenessState(), EnemyAwarenessState::Searching);
    EXPECT_FLOAT_EQ(enemy.movementSpeed(), 90);
    const auto memory = enemy.lastKnownTargetPosition();
    ASSERT_TRUE(memory);
    static_cast<void>(enemy.updateTowardsTarget({1500, 1100}, directive(EnemyTacticalRole::Pressure),
        0.1F, 2000, 1200, false, Vec2{400, 125}));
    EXPECT_FLOAT_EQ(enemy.movementSpeed(), 90);
    ASSERT_TRUE(enemy.lastKnownTargetPosition());
    EXPECT_FLOAT_EQ(enemy.lastKnownTargetPosition()->x, memory->x);
    EXPECT_FLOAT_EQ(enemy.lastKnownTargetPosition()->y, memory->y);
}

TEST(RunnerInfectedRuntimeTest, SoundInvestigationAndUnawarePatrolUse90)
{
    auto heard = runner();
    heard.investigateSound({400, 125});
    pursue(heard, EnemyTacticalRole::Pressure, false);
    EXPECT_EQ(heard.awarenessState(), EnemyAwarenessState::Searching);
    EXPECT_FLOAT_EQ(heard.movementSpeed(), 90);
    auto patrolling = runner();
    static_cast<void>(patrolling.updateTowardsTarget({1500, 1000}, directive(EnemyTacticalRole::Pressure),
        0.1F, 2000, 1200, false, Vec2{400, 125}, true));
    EXPECT_EQ(patrolling.awarenessState(), EnemyAwarenessState::Unaware);
    EXPECT_FALSE(patrolling.lastKnownTargetPosition());
    EXPECT_FLOAT_EQ(patrolling.movementSpeed(), 90);
}

TEST(RunnerInfectedRuntimeTest, VisibleSupportUses90AndNoMovementGetsNoPursuit)
{
    auto support = runner();
    pursue(support, EnemyTacticalRole::Support);
    EXPECT_EQ(support.awarenessState(), EnemyAwarenessState::Alerted);
    EXPECT_FLOAT_EQ(support.movementSpeed(), 90);
    auto stopped = runner({350, 100});
    pursue(stopped);
    EXPECT_FLOAT_EQ(stopped.movementSpeed(), 0);
    EXPECT_EQ(stopped.attackPhase(), EnemyAttackPhase::Idle);
}

TEST(RunnerInfectedRuntimeTest, LegacyOrdinaryAndArmoredMoveAt72)
{
    const auto armor = publishedContentRegistry().enemyCombatDefinition(
        EnemyCombatDefinitionId{"enemy.infected.armored"}).torsoArmor;
    for (bool armored : {false, true})
    {
        Enemy legacy{{100, 100}, {50, 50}, {}, 12, 1, armored ? armor : std::nullopt};
        pursue(legacy);
        EXPECT_FLOAT_EQ(legacy.movementSpeed(), 72);
        EXPECT_FALSE(legacy.variant());
        auto saved = legacy.checkpoint();
        EXPECT_FALSE(saved.variant);
        auto restored = Enemy::restoreCheckpoint(saved);
        ASSERT_TRUE(restored);
        pursue(*restored);
        EXPECT_FLOAT_EQ(restored->movementSpeed(), 72);
    }
    EnemyAiConfig custom;
    custom.normalMoveSpeed = 51;
    custom.attackMoveSpeed = 113;
    EnemyAiState oldCustom{custom};
    EXPECT_FLOAT_EQ(oldCustom.config().normalMoveSpeed, 51);
    EXPECT_FLOAT_EQ(oldCustom.config().attackMoveSpeed, 113);
    EXPECT_FALSE(oldCustom.config().visiblePursuitMoveSpeed);
    const auto oldMove = oldCustom.update({{125, 125}, {400, 125},
        directive(EnemyTacticalRole::Pressure), 0.1F, true});
    EXPECT_GT(oldMove.moveDirection.x, 0.99F);
    EXPECT_FALSE(oldMove.confirmedVisiblePursuit);
}

TEST(RunnerInfectedRuntimeTest, CopyAndCheckpointPreserveFrozenVariantAndLiveState)
{
    auto enemy = runner();
    pursue(enemy);
    ASSERT_FALSE(enemy.takeDamage(1));
    auto copy = enemy;
    EXPECT_EQ(copy.checkpoint(), enemy.checkpoint());
    const auto saved = enemy.checkpoint();
    ASSERT_EQ(saved.variant, runnerEnemyVariantSnapshot());
    auto restored = Enemy::restoreCheckpoint(saved);
    ASSERT_TRUE(restored);
    EXPECT_EQ(restored->checkpoint(), saved);
    for (int n = 0; n < 20; ++n)
    {
        pursue(copy, EnemyTacticalRole::Pressure, n < 10, 0.01F);
        pursue(*restored, EnemyTacticalRole::Pressure, n < 10, 0.01F);
        EXPECT_EQ(restored->checkpoint(), copy.checkpoint());
    }
}

TEST(RunnerInfectedRuntimeTest, MalformedFrozenCheckpointIsRejectedWithoutChangingSource)
{
    auto enemy = runner();
    pursue(enemy);
    const auto source = enemy.checkpoint();
    ASSERT_TRUE(source.visiblePursuitMove);
    auto missing = source; missing.variant.reset();
    EXPECT_FALSE(Enemy::restoreCheckpoint(missing));
    auto speed = source; speed.variant->movement.visiblePursuitMoveSpeed = 271;
    EXPECT_FALSE(Enemy::restoreCheckpoint(speed));
    auto version = source; version.variant->movement.attackProfileVersion = 2;
    EXPECT_FALSE(Enemy::restoreCheckpoint(version));
    auto health = source; health.maximumHealth = 12;
    EXPECT_FALSE(Enemy::restoreCheckpoint(health));
    EXPECT_EQ(enemy.checkpoint(), source);
}

TEST(RunnerInfectedRuntimeTest, ImpactSlowMultipliesActualFastMovementThenExpires)
{
    auto slowed = runner();
    auto normal = runner();
    ASSERT_FALSE(slowed.takeDamage(1));
    pursue(slowed);
    pursue(normal);
    EXPECT_NEAR(slowed.position().x - 100, (normal.position().x - 100) * 0.28F, 0.001F);
    EXPECT_TRUE(slowed.isImpactSlowed());
    pursue(slowed, EnemyTacticalRole::Pressure, true, 0.1F);
    EXPECT_FALSE(slowed.isImpactSlowed());
    const auto before = slowed.position();
    pursue(slowed);
    EXPECT_NEAR(slowed.position().x - before.x, 27, 0.001F);
}

TEST(RunnerInfectedRuntimeTest, EngageScratchKeepsOriginalAttackPhasesAndDamage)
{
    auto fast = runner({100, 100});
    Enemy legacy{{100, 100}, {50, 50}, {}, 12, 2};
    for (auto *enemy : {&fast, &legacy})
        static_cast<void>(enemy->updateTowardsTarget({180, 125}, directive(EnemyTacticalRole::Engage, true),
            0.01F, 2000, 1200));
    ASSERT_EQ(fast.attackType(), EnemyAttackType::Scratch);
    ASSERT_EQ(legacy.attackType(), EnemyAttackType::Scratch);
    ASSERT_TRUE(fast.attackConfig());
    expectSameAttackConfig(*fast.attackConfig(), *legacy.attackConfig());
    const EnemyAttackConfig frozenScratch{0.18F, 0.10F, 0.28F, 0, 0,
        {64, 48}, 42, 1, 0};
    expectSameAttackConfig(*fast.attackConfig(), frozenScratch);
    expectSameAttackConfig(standardInfectedAttackConfigsV1()[1], frozenScratch);
    const auto saved = fast.checkpoint();
    EXPECT_FLOAT_EQ(saved.scratchCooldown, 0.65F);
    EXPECT_TRUE(saved.specialChargeArmed);
    auto restored = Enemy::restoreCheckpoint(saved);
    ASSERT_TRUE(restored);
    expectSameAttackConfig(*restored->attackConfig(), frozenScratch);
    EXPECT_FLOAT_EQ(restored->checkpoint().scratchCooldown, 0.65F);
    const auto damage = standardInfectedAttackCombatDamageV1(EnemyAttackType::Scratch);
    EXPECT_EQ(damage.baseDamage, 12);
    EXPECT_EQ(damage.region, HitRegion::Torso);
    EXPECT_EQ(damage.penetration, 1);
    EXPECT_EQ(damage.armorDamage, 2);
    for (int n = 0; n < 100; ++n)
    {
        for (auto *enemy : {&fast, &legacy, &*restored})
            static_cast<void>(enemy->updateTowardsTarget({180, 125},
                directive(EnemyTacticalRole::Engage), 0.01F, 2000, 1200));
        EXPECT_EQ(fast.attackPhase(), legacy.attackPhase());
        EXPECT_FLOAT_EQ(fast.attackPhaseRemaining(), legacy.attackPhaseRemaining());
        EXPECT_EQ(fast.hasAttackHitOpportunity(), legacy.hasAttackHitOpportunity());
        EXPECT_EQ(fast.checkpoint(), restored->checkpoint());
        if (n == 63) EXPECT_NEAR(restored->checkpoint().scratchCooldown, 0.01F, 0.00001F);
    }
    EXPECT_FLOAT_EQ(restored->checkpoint().scratchCooldown, 0);
}

TEST(RunnerInfectedRuntimeTest, GrabLungeAndBiteKeepOriginal74Point25AndPoint55)
{
    auto fast = runner();
    Enemy legacy{{100, 100}, {50, 50}, {}, 12, 2};
    for (auto *enemy : {&fast, &legacy}) ASSERT_TRUE(enemy->tryStartAttack(EnemyAttackType::Grab, {1, 0}));
    ASSERT_TRUE(fast.attackConfig());
    const auto config = *fast.attackConfig();
    EXPECT_FLOAT_EQ(config.lungeDistance, 74.25F);
    EXPECT_FLOAT_EQ(config.activeDuration, 0.55F);
    const EnemyAttackConfig frozenGrab{0.55F, 0.55F, 0.35F, 1.35F, 74.25F,
        {50, 50}, 0, 0, 0};
    expectSameAttackConfig(config, frozenGrab);
    expectSameAttackConfig(standardInfectedAttackConfigsV1()[0], frozenGrab);
    expectSameAttackConfig(config, *legacy.attackConfig());
    for (auto *enemy : {&fast, &legacy})
    {
        EXPECT_EQ(enemy->attackPhase(), EnemyAttackPhase::Windup);
        EXPECT_FLOAT_EQ(enemy->movementSpeed(), 72);
        EXPECT_FLOAT_EQ(enemy->velocity().x, 72);
        static_cast<void>(enemy->update(config.windupDuration / 2, 2000, 1200));
        EXPECT_EQ(enemy->attackPhase(), EnemyAttackPhase::Windup);
        EXPECT_FLOAT_EQ(enemy->movementSpeed(), 72);
        EXPECT_NEAR(enemy->position().x - 100, 19.8F, 0.001F);
        static_cast<void>(enemy->update(config.windupDuration / 2, 2000, 1200));
        EXPECT_NEAR(enemy->position().x - 100, 39.6F, 0.001F);
    }
    ASSERT_EQ(fast.attackPhase(), EnemyAttackPhase::Active);
    ASSERT_EQ(legacy.attackPhase(), EnemyAttackPhase::Active);
    EXPECT_FLOAT_EQ(fast.movementSpeed(), 135);
    EXPECT_FLOAT_EQ(legacy.movementSpeed(), 135);
    const auto start = fast.position();
    const auto legacyStart = legacy.position();
    auto fullFast = fast;
    auto fullLegacy = legacy;
    for (auto *enemy : {&fullFast, &fullLegacy})
        static_cast<void>(enemy->update(config.activeDuration, 2000, 1200));
    EXPECT_NEAR(fullFast.position().x - start.x, 74.25F, 0.001F);
    EXPECT_NEAR(fullLegacy.position().x - legacyStart.x, 74.25F, 0.001F);
    EXPECT_EQ(fullFast.attackPhase(), EnemyAttackPhase::OffBalance);
    EXPECT_EQ(fullLegacy.attackPhase(), EnemyAttackPhase::OffBalance);
    EXPECT_FLOAT_EQ(fullFast.attackPhaseRemaining(), 1.35F);
    EXPECT_FLOAT_EQ(fullLegacy.attackPhaseRemaining(), 1.35F);
    for (auto *enemy : {&fast, &legacy})
        static_cast<void>(enemy->update(config.activeDuration / 2, 2000, 1200));
    EXPECT_NEAR(fast.position().x - start.x, 37.125F, 0.001F);
    EXPECT_NEAR(legacy.position().x - legacyStart.x, 37.125F, 0.001F);
    for (auto *enemy : {&fast, &legacy}) ASSERT_TRUE(enemy->confirmGrabContact());
    ASSERT_EQ(fast.attackType(), EnemyAttackType::Bite);
    expectSameAttackConfig(*fast.attackConfig(), *legacy.attackConfig());
    const EnemyAttackConfig frozenBite{0.25F, 0.12F, 0.60F, 0, 0,
        {52, 44}, 38, 2, 0.75F};
    expectSameAttackConfig(*fast.attackConfig(), frozenBite);
    expectSameAttackConfig(standardInfectedAttackConfigsV1()[2], frozenBite);
    const auto biteDamage = standardInfectedAttackCombatDamageV1(EnemyAttackType::Bite);
    EXPECT_EQ(biteDamage.baseDamage, 18);
    EXPECT_EQ(biteDamage.region, HitRegion::Head);
    EXPECT_EQ(biteDamage.penetration, 1);
    EXPECT_EQ(biteDamage.armorDamage, 3);

    // Stage an already armed, almost completed charge. The real AI must wait
    // for the original 0.50-second threshold, then record the original 4 s
    // cooldown; checkpoint restoration must preserve both policies and state.
    auto chargeState = runner().checkpoint();
    chargeState.specialChargeArmed = true;
    chargeState.specialChargeHold = 0.49F;
    auto charging = Enemy::restoreCheckpoint(chargeState);
    ASSERT_TRUE(charging);
    static_cast<void>(charging->updateTowardsTarget({275, 125},
        directive(EnemyTacticalRole::Engage, true), 0.009F, 2000, 1200));
    EXPECT_EQ(charging->attackPhase(), EnemyAttackPhase::Idle);
    EXPECT_NEAR(charging->checkpoint().specialChargeHold, 0.499F, 0.00001F);
    ASSERT_TRUE(charging->setPosition({100, 100}));
    static_cast<void>(charging->updateTowardsTarget({275, 125},
        directive(EnemyTacticalRole::Engage, true), 0.002F, 2000, 1200));
    ASSERT_EQ(charging->attackType(), EnemyAttackType::Grab);
    EXPECT_FLOAT_EQ(charging->checkpoint().grabCooldown, 4.0F);
    EXPECT_FLOAT_EQ(charging->checkpoint().specialChargeHold, 0);
    EXPECT_FALSE(charging->checkpoint().specialChargeArmed);
    auto resumed = Enemy::restoreCheckpoint(charging->checkpoint());
    ASSERT_TRUE(resumed);
    expectSameAttackConfig(*resumed->attackConfig(), frozenGrab);
    static_cast<void>(resumed->updateTowardsTarget({275, 125},
        directive(EnemyTacticalRole::Engage), 0.1F, 2000, 1200));
    EXPECT_NEAR(resumed->checkpoint().grabCooldown, 3.9F, 0.00001F);
}

TEST(RunnerInfectedRuntimeTest, ActualSquadPressureDoesNotIncreaseConcurrentAttackQuota)
{
    std::vector<EnemySquadMemberSnapshot> members(15, {{100, 100}, true,
        EnemyAwarenessState::Alerted, EnemyAttackPhase::Idle, true});
    EnemySquadCoordinator squad;
    const auto decisions = squad.decide(members, {400, 100});
    const auto allowed = std::count_if(decisions.begin(), decisions.end(),
        [](const auto &d) { return d.canStartAttack; });
    EXPECT_LE(static_cast<std::size_t>(allowed), squad.config().maximumConcurrentAttackers);
    bool pressure = false;
    for (const auto &decision : decisions)
    {
        if (decision.role != EnemyTacticalRole::Pressure) continue;
        pressure = true;
        EXPECT_FALSE(decision.canStartAttack);
        auto enemy = runner();
        static_cast<void>(enemy.updateTowardsTarget({400, 125}, decision, 0.1F, 2000, 1200));
        EXPECT_EQ(enemy.attackPhase(), EnemyAttackPhase::Idle);
        EXPECT_FLOAT_EQ(enemy.movementSpeed(), 270);
    }
    EXPECT_TRUE(pressure);
}

TEST(RunnerInfectedRuntimeTest, HighSpeedRealWorldCannotCrossThinWallOrCorner)
{
    for (const auto walls : std::vector<std::vector<BallisticBlocker>>{
        {{1, {{400, 200}, {0.5F, 150}}}},
        {{1, {{400, 200}, {0.5F, 150}}}, {2, {{400, 350}, {75, 0.5F}}}}})
    {
        auto config = arena();
        config.playerSpawn = {600, 400};
        config.initialEnemies.push_back(spawn({300, 300}));
        config.ballisticBlockers = walls;
        GameplayWorld world{config};
        auto &enemy = RunnerInfectedTestAccess::actors(world).front();
        enemy.hearTarget({616, 416});
        bool moved = false, fast = false;
        Vec2 previous = enemy.position();
        for (int frame = 0; frame < 240; ++frame)
        {
            world.update({}, 1.0F / 30);
            ASSERT_EQ(world.enemies().size(), 1U);
            const auto &current = world.enemies().front();
            moved |= std::hypot(current.position().x - previous.x, current.position().y - previous.y) > 0.01F;
            fast |= current.attackPhase() == EnemyAttackPhase::Idle && current.movementSpeed() > 269;
            for (const auto &wall : walls) EXPECT_FALSE(overlaps(current.bounds(), wall.bounds));
            previous = current.position();
        }
        EXPECT_TRUE(moved);
        EXPECT_TRUE(fast);
    }
}

TEST(RunnerInfectedRuntimeTest, FirstMiddleLastDeathRetiresStableIdentityExactlyOnce)
{
    for (std::size_t victimIndex : {0U, 2U, 4U})
    {
        SCOPED_TRACE(victimIndex);
        auto config = arena();
        for (int n = 0; n < 5; ++n) config.initialEnemies.push_back(spawn({2000.0F + n * 200, 1000}));
        GameplayWorld world{config};
        std::array<CombatTargetId, 5> ids{};
        for (std::size_t n = 0; n < ids.size(); ++n) ids[n] = world.enemies()[n].combatTargetId();
        const auto victim = ids[victimIndex];
        queueHit(world, victim, 100);
        world.update({}, 0.000001F);
        EXPECT_EQ(world.enemies().size(), 4U);
        EXPECT_FALSE(RunnerInfectedTestAccess::actors(world).hasState(victim));
        EXPECT_EQ(std::count_if(world.hitResultsLastUpdate().begin(), world.hitResultsLastUpdate().end(),
            [victim](const auto &hit) { return hit.targetId == victim && hit.targetKilled; }), 1);
        const auto score = world.score();
        EXPECT_GT(score, 0);
        for (auto id : ids)
        {
            if (id == victim) continue;
            const auto *survivor = RunnerInfectedTestAccess::actors(world).find(id);
            ASSERT_NE(survivor, nullptr);
            EXPECT_EQ(survivor->variant(), runnerEnemyVariantSnapshot());
            EXPECT_EQ(survivor->health(), 8);
        }
        EXPECT_TRUE(world.pendingCorpseDeaths().empty());
        world.update({}, 0.000001F);
        EXPECT_EQ(world.score(), score);
        EXPECT_TRUE(world.hitResultsLastUpdate().empty());
        EXPECT_FALSE(RunnerInfectedTestAccess::actors(world).hasState(victim));
    }
}

TEST(RunnerInfectedRuntimeTest, PortalRoundTripPreservesRunnerAndDoesNotReviveDeadRunner)
{
    auto config = arena();
    config.initialEnemies.push_back(spawn({2000, 1000}));
    RaidInteriorWorldConfig inside;
    inside.id = RaidSpaceDefinitionId{"raid_space.runner.test"};
    inside.displayName = "Runner test"; inside.worldSize = {800, 600};
    inside.exteriorEntrance = {{80, 80}, {100, 100}};
    inside.exteriorReturn = {100, 100}; inside.interiorSpawn = {80, 80};
    inside.interiorExit = {{60, 60}, {120, 120}};
    inside.initialEnemies.push_back(spawn({500, 400}, false));
    config.interiors.push_back(inside);
    GameplayWorld world{config};
    const auto snapshot = world.enemies().front().checkpoint();
    GameplayInput portal; portal.interactJustPressed = true;
    world.update(portal, 0);
    ASSERT_FALSE(world.inOutdoorRaidSpace());
    ASSERT_EQ(world.enemies().size(), 1U);
    EXPECT_FALSE(isRunnerEnemyVariant(world.enemies().front().variant()));
    world.update(portal, 0);
    ASSERT_TRUE(world.inOutdoorRaidSpace());
    ASSERT_EQ(world.enemies().size(), 1U);
    EXPECT_EQ(world.enemies().front().checkpoint(), snapshot);
    queueHit(world, snapshot.id, 100);
    world.update({}, 0.000001F);
    ASSERT_TRUE(world.enemies().empty());
    const auto score = world.score();
    world.update(portal, 0);
    world.update(portal, 0);
    EXPECT_TRUE(world.inOutdoorRaidSpace());
    EXPECT_TRUE(world.enemies().empty());
    EXPECT_EQ(world.score(), score);
    EXPECT_TRUE(world.pendingCorpseDeaths().empty());
}
