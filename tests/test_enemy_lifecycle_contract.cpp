#include "base_world.h"
#include "game_session.h"
#include "game_flow.h"
#include "gameplay_world.h"
#include <array>
#include <chrono>
#include <filesystem>
#include <gtest/gtest.h>
#include <memory>
#include <type_traits>

enum class Activity { Daily, Defense, Raid };

// Fixture access only arranges deterministic actors, attachments and in-flight
// shots. Every frame executes the real activity update and its production
// shooting/lifecycle path, not a mock resolver or a second simulation loop.
struct EnemyLifecycleTestAccess {
  Activity activity;
  std::unique_ptr<BaseWorld> daily;
  std::unique_ptr<BaseDefenseRuntime> defense;
  std::unique_ptr<GameplayWorld> raid;
  WorldShootingRuntime defenseShooting;
  HomePerimeterSiteSnapshot perimeter;
  std::array<CombatTargetId, 3> ids{1, 2, 3};

  explicit EnemyLifecycleTestAccess(Activity kind, int health = 3) : activity(kind) {
    const std::array<Vec2, 3> positions{
        {{2000, 2000}, {2200, 2000}, {2400, 2000}}};
    if (kind == Activity::Daily) {
      daily = std::make_unique<BaseWorld>();
      perimeter.baseSiteDefinitionId =
          RegionalBaseSiteDefinitionId{daily->siteDefinitionId()};
      perimeter.cycleIndex = 4;
      for (std::size_t i = 0; i < ids.size(); ++i)
        perimeter.enemies.push_back({static_cast<std::uint32_t>(ids[i]),
                                     positions[i],
                                     positions[i],
                                     {50, 50},
                                     health,
                                     health});
      daily->configureHomePerimeter(&perimeter);
      daily->playerPosition_ = {100, 100};
      daily->movementBlockers_.clear();
      daily->movementBlockerIndex_ =
          RaidSpaceBlockerIndex::build(daily->worldSize(), {});
    } else if (kind == Activity::Raid) {
      std::vector<EnemySpawn> spawns;
      for (auto p : positions)
        spawns.push_back(EnemySpawn{p, {50, 50}, health});
      raid = std::make_unique<GameplayWorld>(spawns, 100);
      raid->worldSize_ = {6000, 6000};
      static_cast<void>(raid->player_.setPosition({100, 100}));
      raid->ballisticBlockers_.clear();
      raid->outdoorBlockerIndex_ =
          RaidSpaceBlockerIndex::build({6000, 6000}, {});
      for (const auto &enemy : raid->enemies_) {
        auto &nav = raid->enemies_.state(enemy.combatTargetId()).navigation;
        nav.goal = Vec2{2025, 2025};
        nav.waypoint = enemy.position();
        nav.refreshRemainingSeconds = 100;
      }
    } else {
      BaseDefenseSnapshot s;
      s.eventId = "lifecycle-contract";
      s.siegeSequence = 1;
      s.seed = 12345;
      s.frozenPopulation = 8;
      s.siteDefinitionId = "regional_base_site.greyline_yard";
      s.layoutIdentity = "lifecycle-arena";
      s.worldSize = {6000, 6000};
      s.safeCore = {{2300, 2300}, {1200, 1000}};
      s.playerPosition = {2800, 2700};
      const auto prepared = BaseDefenseRuntime::prepare(s, {}, publishedContentRegistry().enemyCombatDefinition(ordinaryInfectedDefinitionId()));
      if (!prepared)
        throw std::runtime_error("invalid test arena");
      defense = std::make_unique<BaseDefenseRuntime>();
      if (!defense->resume(*prepared, {}))
        throw std::runtime_error("invalid test resume");
      defense->state_.nextSpawnDelay = 1000;
      defense->state_.spawnedEnemyCount = 3;
      for (std::size_t i = 0; i < ids.size(); ++i) {
        ids[i] = prepared->wavePlans.front().enemyIds[i];
        defense->enemies_.spawn(
            Enemy{positions[i], {50, 50}, {}, health, ids[i]},
            {checkpointPoint({positions[i].x + 25, positions[i].y + 25}), 100,
             0.1F});
      }
      defense->synchronizeActorCheckpoints();
    }
  }

  EnemyLifecycle &actors() {
    if (daily)
      return daily->perimeterEnemies_;
    if (raid)
      return raid->activeEnemies();
    return defense->enemies_;
  }
  WorldShootingRuntime &shooting() {
    if (daily)
      return daily->shooting_;
    if (raid)
      return raid->shooting_;
    return defenseShooting;
  }
  static WorldShootingRuntime &baseShooting(BaseWorld &world) {
    return world.shooting_;
  }
  bool attached(CombatTargetId id) {
    if (daily)
      return daily->perimeterEnemies_.hasState(id);
    if (raid)
      return raid->activeEnemies().hasState(id);
    return defense->enemies_.hasState(id);
  }
  void tick() {
    constexpr float dt =
        0.000001F; // preserve position while processing a real frame
    if (daily)
      static_cast<void>(daily->update(GameplayInput{}, dt));
    else if (raid)
      raid->update(GameplayInput{}, dt);
    else {
      defenseShooting.beginFrame(dt);
      defense->advance({}, dt, {2800, 2700}, {40, 52}, false, defenseShooting,
                       {});
    }
  }
  void queueHit(CombatTargetId id, int damage, unsigned count = 1) {
    const Enemy *target = actors().find(id);
    if (!target)
      throw std::runtime_error("missing test target");
    const Vec2 p{target->position().x + 25, target->position().y + 25};
    auto s = shooting().checkpoint();
    for (unsigned i = 0; i < count; ++i) {
      LogicalFlightCheckpoint f;
      f.id = s.nextShotId++;
      f.origin = f.position = checkpointPoint(p);
      f.direction = {1, 0};
      f.impact = {p.x + 100, p.y};
      f.speed = 6000;
      f.extent = 1;
      f.maximumDistance = 100;
      f.damage = damage;
      f.tracerLifetime = 0.05F;
      s.flights.push_back(f);
    }
    if (!shooting().restoreCheckpoint(s))
      throw std::runtime_error("invalid test flight");
  }
  void hit(CombatTargetId id, int damage, unsigned count = 1) {
    queueHit(id, damage, count);
    tick();
  }
  unsigned deaths() const {
    const WorldShootingRuntime &s = daily  ? daily->shooting_
                                    : raid ? raid->shooting_
                                           : defenseShooting;
    return static_cast<unsigned>(std::count_if(
        s.hitResultsLastUpdate().begin(), s.hitResultsLastUpdate().end(),
        [](const HitResult &hit) { return hit.targetKilled; }));
  }

  void combatTick(const GameplayInput &input, float dt) {
    if (daily) static_cast<void>(daily->update(input, dt));
    else if (raid) raid->update(input, dt);
    else {
      defenseShooting.beginFrame(dt);
      const Vec2 center{combatPlayerPosition.x + 20, combatPlayerPosition.y + 26};
      defenseShooting.updateAim(input, center, {1, 0}, {6000, 6000}, dt);
      defense->advance(input, dt, combatPlayerPosition, {40, 52}, false,
                       defenseShooting, {});
    }
  }
  Vec2 combatPlayerPosition{};
  void placeCombatPlayer(Vec2 position) {
    combatPlayerPosition = position;
    if (daily) daily->playerPosition_ = position;
    if (raid) static_cast<void>(raid->player_.setPosition(position));
  }
  void prepareIncomingAttack(EnemyAttackType type, std::size_t index = 0) {
    combatPlayerPosition = {1060, 1000};
    if (daily) {
      daily->playerPosition_ = combatPlayerPosition;
      daily->layout_.baseParcel = {{5000, 5000}, {300, 300}};
    }
    if (raid) {
      static_cast<void>(raid->player_.setPosition(combatPlayerPosition));
      raid->deferPlayerDamageResolution_ = true;
    }
    auto &enemy = actors()[index];
    static_cast<void>(enemy.setPosition({1000, 1000}));
    const auto initialType = type == EnemyAttackType::Bite ? EnemyAttackType::Grab : type;
    if (!enemy.tryStartAttack(initialType, {1, 0})) throw std::runtime_error("attack setup");
    static_cast<void>(enemy.update(enemy.attackConfig()->windupDuration + 0.00001F, 6000, 6000));
    static_cast<void>(enemy.setPosition({1000, 1000}));
    if (type == EnemyAttackType::Bite && !enemy.confirmGrabContact())
      throw std::runtime_error("bite setup");
  }
  std::vector<PlayerDamageObservation> incoming() {
    if (raid) return raid->takePlayerDamageObservations();
    const auto fact = daily ? daily->perimeterDamageObservation()
                            : defense->damageObservationLastUpdate();
    return fact ? std::vector<PlayerDamageObservation>{*fact} : std::vector<PlayerDamageObservation>{};
  }
  void setProtection(float seconds) {
    if (daily) daily->perimeterDamageProtectionRemainingSeconds_ = seconds;
    else if (raid) raid->enemyDamageProtectionRemainingSeconds_ = seconds;
    else defense->state_.damageProtectionSeconds = seconds;
  }
  void restoreAttackWindup() {
    auto state = actors()[0].checkpoint();
    state.attackPhase = static_cast<std::uint32_t>(EnemyAttackPhase::Windup);
    state.attackRemaining = 0.18F;
    state.hitConsumed = state.activeOpportunityPending = false;
    auto restored = Enemy::restoreCheckpoint(state);
    if (!restored) throw std::runtime_error("windup restore");
    actors()[0] = std::move(*restored);
  }
  void prepareOccludedNoiseScene() {
    placeCombatPlayer({1000, 2000});
    const std::vector<BallisticBlocker> wall{{1, {{1500, 1800}, {20, 450}}}};
    if (daily) {
      daily->layout_.baseParcel = {{5000, 5000}, {300, 300}};
      daily->movementBlockers_ = wall;
      daily->movementBlockerIndex_ = RaidSpaceBlockerIndex::build(daily->worldSize(), wall);
    } else if (defense) {
      defense->blockerIndex_ = RaidSpaceBlockerIndex::build({6000, 6000}, wall);
    }
  }
  float protection() const {
    if (daily) return daily->perimeterDamageProtectionRemainingSeconds_;
    if (raid) return raid->enemyDamageProtectionRemainingSeconds_;
    return defense->state_.damageProtectionSeconds;
  }
  bool playerControlled() const { return raid && raid->player_.isControlled(); }
  void blockContactLine() {
    const std::vector<BallisticBlocker> wall{{1, {{1053, 990}, {4, 90}}}};
    if (daily) {
      daily->movementBlockers_ = wall;
      daily->movementBlockerIndex_ = RaidSpaceBlockerIndex::build(daily->worldSize(), wall);
    } else if (raid) {
      raid->ballisticBlockers_ = wall;
      raid->outdoorBlockerIndex_ = RaidSpaceBlockerIndex::build(raid->worldSize_, wall);
    } else {
      defense->blockerIndex_ = RaidSpaceBlockerIndex::build({6000, 6000}, wall);
    }
  }
  void aimAndFire(float localY, bool aimPastTarget) {
    const Vec2 p = actors().front().position();
    const Vec2 size = daily ? daily->playerSize() : raid
        ? Vec2{raid->player_.size(), raid->player_.size()} : Vec2{40, 52};
    combatPlayerPosition = {p.x - 150, p.y + localY - size.y * 0.5F};
    if (daily) daily->playerPosition_ = combatPlayerPosition;
    if (raid) static_cast<void>(raid->player_.setPosition(combatPlayerPosition));
    auto weapon = *itemDefinition(ItemId::Rifle).weaponUse;
    weapon.baseDamage = 4;
    auto handling = deriveWeaponHandling(weapon);
    handling.minimumSpreadDegrees = handling.maximumSpreadDegrees = 0;
    shooting().configureWeapon(weapon, handling, false);
    GameplayInput aim;
    aim.aimWorldPosition = Vec2{p.x + (aimPastTarget ? 100.0F : 25.0F), p.y + localY};
    combatTick(aim, 0.000001F);
    aim.fireJustPressed = true;
    combatTick(aim, 0.001F);
  }
};

class EnemyLifecycleContract : public testing::TestWithParam<Activity> {};

TEST_P(EnemyLifecycleContract, IncomingScratchAndBitePreserveFullDamageFact) {
  for (const auto type : {EnemyAttackType::Scratch, EnemyAttackType::Bite}) {
    EnemyLifecycleTestAccess fixture{GetParam(), 12};
    fixture.prepareIncomingAttack(type);
    fixture.combatTick({}, 0.000001F);
    const auto facts = fixture.incoming();
    ASSERT_EQ(facts.size(), 1U);
    EXPECT_EQ(facts.front(), enemyAttackDamageObservation(fixture.ids[0], type));
    EXPECT_EQ(facts.front().baseDamage, type == EnemyAttackType::Scratch ? 12 : 18);
    fixture.combatTick({}, 0.000001F);
    EXPECT_TRUE(fixture.incoming().empty());
  }
}

TEST_P(EnemyLifecycleContract, SimultaneousAttacksPreserveDamageProtection) {
  EnemyLifecycleTestAccess fixture{GetParam(), 12};
  fixture.prepareIncomingAttack(EnemyAttackType::Scratch, 0);
  fixture.prepareIncomingAttack(EnemyAttackType::Scratch, 1);
  fixture.combatTick({}, 0.000001F);
  const auto facts = fixture.incoming();
  ASSERT_EQ(facts.size(), 1U);
  EXPECT_EQ(facts[0].sourceEnemyId, fixture.ids[0]);
  fixture.combatTick({}, 0.01F);
  EXPECT_TRUE(fixture.incoming().empty());
}

TEST_P(EnemyLifecycleContract, EnemyContactPrecedesSameFrameLethalShot) {
  EnemyLifecycleTestAccess fixture{GetParam(), 12};
  fixture.prepareIncomingAttack(EnemyAttackType::Scratch);
  fixture.queueHit(fixture.ids[0], 100);
  fixture.combatTick({}, 0.000001F);
  EXPECT_EQ(fixture.actors().find(fixture.ids[0]), nullptr);
  const auto facts = fixture.incoming();
  // Explicit behavior migration from #154: all three adapters now preserve
  // contact accepted before the same frame's shot resolution/removal.
  ASSERT_EQ(facts.size(), 1U);
  EXPECT_EQ(facts[0], enemyAttackDamageObservation(fixture.ids[0], EnemyAttackType::Scratch));
  EXPECT_EQ(fixture.deaths(), 1U);
  EXPECT_FALSE(fixture.attached(fixture.ids[0]));
  fixture.combatTick({}, 0.001F);
  EXPECT_TRUE(fixture.incoming().empty());
  EXPECT_EQ(fixture.deaths(), 0U);
  EXPECT_EQ(fixture.actors().find(fixture.ids[0]), nullptr);
}

TEST_P(EnemyLifecycleContract, KillBeforeAttackWindowPreventsFutureContact) {
  EnemyLifecycleTestAccess fixture{GetParam(), 12};
  fixture.prepareIncomingAttack(EnemyAttackType::Scratch);
  fixture.restoreAttackWindup();
  fixture.queueHit(fixture.ids[0], 100);
  fixture.combatTick({}, 0.000001F);
  EXPECT_TRUE(fixture.incoming().empty());
  EXPECT_EQ(fixture.deaths(), 1U);
  EXPECT_EQ(fixture.actors().find(fixture.ids[0]), nullptr);
  for (int i = 0; i < 5; ++i) {
    fixture.combatTick({}, 0.05F);
    EXPECT_TRUE(fixture.incoming().empty());
    EXPECT_EQ(fixture.deaths(), 0U);
    EXPECT_EQ(fixture.actors().find(fixture.ids[0]), nullptr);
  }
}

TEST_P(EnemyLifecycleContract, NonLethalShotAndContactBothPreserveStableIdentity) {
  EnemyLifecycleTestAccess fixture{GetParam(), 12};
  fixture.prepareIncomingAttack(EnemyAttackType::Scratch);
  fixture.queueHit(fixture.ids[0], 1);
  fixture.combatTick({}, 0.000001F);
  const auto facts = fixture.incoming();
  ASSERT_EQ(facts.size(), 1U);
  EXPECT_EQ(facts[0].sourceEnemyId, fixture.ids[0]);
  const auto *enemy = fixture.actors().find(fixture.ids[0]);
  ASSERT_NE(enemy, nullptr);
  EXPECT_EQ(enemy->health(), 11);
  EXPECT_TRUE(enemy->checkpoint().hitConsumed);
  EXPECT_TRUE(fixture.attached(fixture.ids[0]));
  EXPECT_EQ(fixture.deaths(), 0U);
  fixture.combatTick({}, 0.01F);
  EXPECT_TRUE(fixture.incoming().empty());
}

TEST_P(EnemyLifecycleContract, ProtectedContactFollowedByKillDoesNotCreateDamageFact) {
  EnemyLifecycleTestAccess fixture{GetParam(), 12};
  fixture.prepareIncomingAttack(EnemyAttackType::Bite);
  fixture.setProtection(0.2F);
  fixture.queueHit(fixture.ids[0], 100, 3);
  fixture.combatTick({}, 0.000001F);
  EXPECT_TRUE(fixture.incoming().empty());
  EXPECT_FALSE(fixture.playerControlled());
  EXPECT_EQ(fixture.deaths(), 1U);
  EXPECT_EQ(fixture.actors().find(fixture.ids[0]), nullptr);
  fixture.setProtection(0);
  fixture.combatTick({}, 0.000001F);
  EXPECT_TRUE(fixture.incoming().empty());
  EXPECT_EQ(fixture.deaths(), 0U);
}

TEST_P(EnemyLifecycleContract, ShotImpactDoesNotRetroactivelySlowCurrentEnemyMovement) {
  EnemyLifecycleTestAccess hit{GetParam(), 12}, quiet{GetParam(), 12};
  hit.prepareIncomingAttack(EnemyAttackType::Grab);
  quiet.prepareIncomingAttack(EnemyAttackType::Grab);
  hit.placeCombatPlayer({1400, 1000});
  quiet.placeCombatPlayer({1400, 1000});
  hit.queueHit(hit.ids[0], 1);
  hit.combatTick({}, 0.03F);
  quiet.combatTick({}, 0.03F);
  const auto *wounded = hit.actors().find(hit.ids[0]);
  const auto *unharmed = quiet.actors().find(quiet.ids[0]);
  ASSERT_NE(wounded, nullptr);
  ASSERT_NE(unharmed, nullptr);
  EXPECT_EQ(wounded->health(), 11);
  EXPECT_TRUE(wounded->isImpactSlowed());
  EXPECT_GT(wounded->position().x, 1000);
  EXPECT_FLOAT_EQ(wounded->position().x, unharmed->position().x);
  EXPECT_FLOAT_EQ(wounded->position().y, unharmed->position().y);
  hit.combatTick({}, 0.03F);
  quiet.combatTick({}, 0.03F);
  EXPECT_LT(hit.actors().find(hit.ids[0])->position().x,
            quiet.actors().find(quiet.ids[0])->position().x);
}

TEST_P(EnemyLifecycleContract, GrabContactImmediatelyConsumesOneBite) {
  EnemyLifecycleTestAccess fixture{GetParam(), 12};
  fixture.prepareIncomingAttack(EnemyAttackType::Grab);
  fixture.placeCombatPlayer({1020, 1000});
  fixture.combatTick({}, 0.000001F);
  const auto facts = fixture.incoming();
  ASSERT_EQ(facts.size(), 1U);
  EXPECT_EQ(facts.front(), enemyAttackDamageObservation(fixture.ids[0], EnemyAttackType::Bite));
  EXPECT_TRUE(fixture.actors()[0].checkpoint().hitConsumed);
  fixture.combatTick({}, 0.000001F);
  EXPECT_TRUE(fixture.incoming().empty());
}

TEST_P(EnemyLifecycleContract, ProtectedContactsCannotQueueDamageOrControl) {
  for (auto type : {EnemyAttackType::Scratch, EnemyAttackType::Grab, EnemyAttackType::Bite}) {
    EnemyLifecycleTestAccess fixture{GetParam(), 12};
    fixture.prepareIncomingAttack(type);
    if (type == EnemyAttackType::Grab) fixture.placeCombatPlayer({1020, 1000});
    fixture.setProtection(0.2F);
    fixture.combatTick({}, 0.000001F);
    EXPECT_TRUE(fixture.incoming().empty());
    EXPECT_TRUE(fixture.actors()[0].checkpoint().hitConsumed);
    EXPECT_NEAR(fixture.protection(), 0.2F - 0.000001F, 0.0000001F);
    EXPECT_FALSE(fixture.playerControlled());
    fixture.setProtection(0);
    fixture.combatTick({}, 0.000001F);
    EXPECT_TRUE(fixture.incoming().empty());
    EXPECT_FALSE(fixture.playerControlled());
    EXPECT_FLOAT_EQ(fixture.protection(), 0);
  }
}

TEST_P(EnemyLifecycleContract, AttackHitboxOverlapCannotHitThroughWall) {
  for (auto type : {EnemyAttackType::Scratch, EnemyAttackType::Bite}) {
    EnemyLifecycleTestAccess fixture{GetParam(), 12};
    fixture.prepareIncomingAttack(type);
    fixture.blockContactLine();
    fixture.combatTick({}, 0.000001F);
    EXPECT_TRUE(fixture.incoming().empty());
    EXPECT_FALSE(fixture.actors()[0].checkpoint().hitConsumed);
    EXPECT_FLOAT_EQ(fixture.protection(), 0);
    EXPECT_FALSE(fixture.playerControlled());
  }
}

TEST_P(EnemyLifecycleContract, SimultaneousScratchAndGrabDoNotAddProtectedControl) {
  EnemyLifecycleTestAccess fixture{GetParam(), 12};
  fixture.prepareIncomingAttack(EnemyAttackType::Scratch, 0);
  fixture.prepareIncomingAttack(EnemyAttackType::Grab, 1);
  fixture.placeCombatPlayer({1038, 1000});
  fixture.combatTick({}, 0.000001F);
  const auto facts = fixture.incoming();
  ASSERT_EQ(facts.size(), 1U);
  EXPECT_EQ(facts[0], enemyAttackDamageObservation(fixture.ids[0], EnemyAttackType::Scratch));
  EXPECT_TRUE(fixture.actors()[0].checkpoint().hitConsumed);
  EXPECT_TRUE(fixture.actors()[1].checkpoint().hitConsumed);
  EXPECT_FALSE(fixture.playerControlled());
}

TEST_P(EnemyLifecycleContract, NonLethalPreservesIdentityAndAttachedState) {
  EnemyLifecycleTestAccess fixture{GetParam()};
  fixture.hit(fixture.ids[1], 1);
  ASSERT_EQ(fixture.actors().size(), 3U);
  ASSERT_NE(fixture.actors().find(fixture.ids[1]), nullptr);
  EXPECT_EQ(fixture.actors().find(fixture.ids[1])->health(), 2);
  EXPECT_TRUE(fixture.attached(fixture.ids[1]));
  EXPECT_EQ(fixture.deaths(), 0U);
}

TEST_P(EnemyLifecycleContract, SameRealShotProducesSameDamageAndSpecialFeedback) {
  // Fresh adapters for torso, true head and a ray crossing the head without
  // fire-time reticle intent. Fire/aim/sweep are production paths, not fake hits.
  for (unsigned scenario = 0; scenario < 3; ++scenario) {
    EnemyLifecycleTestAccess fixture{GetParam(), 12};
    fixture.aimAndFire(scenario == 0 ? 25.0F : 5.0F, scenario == 2);
    for (int i = 0; i < 80 && fixture.shooting().hitResultsLastUpdate().empty(); ++i)
      fixture.combatTick({}, 0.005F);
    const auto &hits = fixture.shooting().hitResultsLastUpdate();
    ASSERT_EQ(hits.size(), 1U) << scenario;
    const bool head = scenario == 1;
    EXPECT_EQ(hits[0].targetId, fixture.ids[0]);
    EXPECT_EQ(hits[0].semantic, head ? HitSemantic::Headshot : HitSemantic::Normal);
    EXPECT_EQ(hits[0].damageApplied, head ? 8 : 4);
    ASSERT_NE(fixture.actors().find(fixture.ids[0]), nullptr);
    EXPECT_EQ(fixture.actors().find(fixture.ids[0])->health(), head ? 4 : 8);
    const auto feedback = fixture.shooting().hitFeedbackPresentation();
    EXPECT_EQ(feedback.semantic, hits[0].semantic);
    EXPECT_FLOAT_EQ(feedback.remainingSeconds, head ? 0.18F : 0.0F);
    if (fixture.daily) EXPECT_EQ(fixture.daily->hitFeedbackPresentation(), feedback);
    if (fixture.raid) EXPECT_EQ(fixture.raid->hitFeedbackPresentation(), feedback);
    fixture.combatTick({}, 0.05F);
    EXPECT_NEAR(fixture.shooting().hitFeedbackPresentation().remainingSeconds,
                head ? 0.13F : 0.0F, 0.0001F);
    for (int i = 0; i < 4; ++i) fixture.combatTick({}, 0.05F);
    EXPECT_EQ(fixture.shooting().hitFeedbackPresentation(), HitFeedbackPresentationSnapshot{});
  }
}

TEST_P(EnemyLifecycleContract, LethalHeadFeedbackSurvivesRemovalButNotSpatialReset) {
  EnemyLifecycleTestAccess fixture{GetParam(), 8};
  fixture.aimAndFire(5.0F, false);
  for (int i = 0; i < 80 && fixture.shooting().hitResultsLastUpdate().empty(); ++i)
    fixture.combatTick({}, 0.005F);
  ASSERT_EQ(fixture.deaths(), 1U);
  EXPECT_EQ(fixture.actors().find(fixture.ids[0]), nullptr);
  EXPECT_FALSE(fixture.attached(fixture.ids[0]));
  EXPECT_EQ(fixture.shooting().hitFeedbackPresentation().semantic, HitSemantic::Headshot);
  const auto checkpoint = fixture.shooting().checkpoint();
  WorldShootingRuntime restored;
  ASSERT_TRUE(restored.restoreCheckpoint(checkpoint));
  EXPECT_EQ(restored.hitFeedbackPresentation(), HitFeedbackPresentationSnapshot{});
  fixture.shooting().clearSpatialTransientPresentation();
  EXPECT_EQ(fixture.shooting().hitFeedbackPresentation(), HitFeedbackPresentationSnapshot{});
  fixture.combatTick({}, 0.01F);
  EXPECT_EQ(fixture.deaths(), 0U);
}

TEST_P(EnemyLifecycleContract, FirstMiddleLastRemovalPreservesSurvivors) {
  for (std::size_t victim = 0; victim < 3; ++victim) {
    EnemyLifecycleTestAccess fixture{GetParam()};
    std::array<Vec2, 3> before;
    for (std::size_t i = 0; i < 3; ++i)
      before[i] = fixture.actors()[i].position();
    fixture.hit(fixture.ids[victim], 3);
    ASSERT_EQ(fixture.actors().size(), 2U);
    EXPECT_EQ(fixture.actors().find(fixture.ids[victim]), nullptr);
    EXPECT_FALSE(fixture.attached(fixture.ids[victim]));
    EXPECT_EQ(fixture.deaths(), 1U);
    for (std::size_t i = 0; i < 3; ++i) {
      if (i == victim)
        continue;
      const auto *survivor = fixture.actors().find(fixture.ids[i]);
      ASSERT_NE(survivor, nullptr);
      EXPECT_FLOAT_EQ(survivor->position().x, before[i].x);
      EXPECT_FLOAT_EQ(survivor->position().y, before[i].y);
      EXPECT_TRUE(fixture.attached(fixture.ids[i]));
    }
    if (fixture.daily)
      fixture.daily->configureHomePerimeter(&fixture.perimeter);
    for (int i = 0; i < 4; ++i)
      fixture.tick();
    EXPECT_EQ(fixture.actors().size(), 2U);
    EXPECT_EQ(fixture.actors().find(fixture.ids[victim]), nullptr);
    EXPECT_EQ(fixture.deaths(), 0U);
  }
}

TEST_P(EnemyLifecycleContract, SameFrameMultipleHitsProduceExactlyOneDeath) {
  EnemyLifecycleTestAccess fixture{GetParam()};
  const auto id = fixture.ids[1];
  fixture.hit(id, 1, 5);
  ASSERT_EQ(fixture.actors().size(), 2U);
  EXPECT_EQ(fixture.deaths(), 1U);
  const auto &hits = fixture.shooting().hitResultsLastUpdate();
  ASSERT_EQ(hits.size(), 3U);
  for (const auto &hit : hits)
    EXPECT_EQ(hit.targetId, id);
  EXPECT_FALSE(fixture.attached(id));
  fixture.tick();
  EXPECT_EQ(fixture.deaths(), 0U);
  EXPECT_EQ(fixture.actors().find(id), nullptr);
}

TEST_P(EnemyLifecycleContract,
       DeadActorImmediatelyLosesAttackAndTargetEligibility) {
  EnemyLifecycleTestAccess fixture{GetParam()};
  Enemy &enemy = fixture.actors()[1];
  const auto id = enemy.combatTargetId();
  ASSERT_TRUE(enemy.tryStartAttack(EnemyAttackType::Scratch, {1, 0}));
  ASSERT_TRUE(enemy.takeDamage(3));
  const Vec2 before = enemy.position();
  EXPECT_FALSE(enemy.hasAttackHitOpportunity());
  EXPECT_FALSE(enemy.hasGrabContactOpportunity());
  EXPECT_FALSE(enemy.consumeAttackHit());
  EXPECT_FALSE(enemy.confirmGrabContact());
  EXPECT_FALSE(enemy.attackHitbox().has_value());
  EXPECT_EQ(fixture.actors().find(id), nullptr);
  static_cast<void>(
      enemy.updateTowardsTarget({100, 100}, {}, 0.1F, 6000, 6000));
  EXPECT_FLOAT_EQ(enemy.position().x, before.x);
  EXPECT_FLOAT_EQ(enemy.position().y, before.y);
  fixture.tick();
  EXPECT_EQ(fixture.actors().find(id), nullptr);
  EXPECT_FALSE(fixture.attached(id));
  EXPECT_EQ(fixture.actors().size(), 2U);
}

INSTANTIATE_TEST_SUITE_P(RealActivities, EnemyLifecycleContract,
                         testing::Values(Activity::Daily, Activity::Defense,
                                         Activity::Raid),
                         [](const auto &info) {
                           return info.param == Activity::Daily     ? "Daily"
                                  : info.param == Activity::Defense ? "Defense"
                                                                    : "Raid";
                         });

TEST(EnemyLifecycleOwnership,
     RejectsDuplicateAndRetiredIdentityWithoutMutation) {
  EnemyRoster<Vec2> actors;
  actors.spawn(Enemy{{10, 10}, {20, 20}, {}, 3, 7}, {40, 40});
  EXPECT_THROW(actors.spawn(Enemy{{}, {20, 20}, {}, 3, 7}),
               std::invalid_argument);
  EXPECT_THROW(actors.spawn(Enemy{{}, {20, 20}}), std::invalid_argument);
  EXPECT_EQ(actors.size(), 1U);
  EXPECT_EQ(actors.state(7).x, 40);
  ASSERT_TRUE(actors[0].takeDamage(3));
  const auto facts = actors.removeDead();
  ASSERT_EQ(facts.size(), 1U);
  EXPECT_EQ(facts.front().id, 7U);
  EXPECT_FALSE(actors.hasState(7));
  EXPECT_TRUE(actors.removeDead().empty());
  EXPECT_THROW(actors.spawn(Enemy{{}, {20, 20}, {}, 3, 7}),
               std::invalid_argument);
  EXPECT_TRUE(actors.empty());
}

TEST(CombatFrameOrder, HomeShotNoiseIsAcceptedOnlyAndDoesNotReplayEnemyMovement) {
  for (auto activity : {Activity::Daily, Activity::Defense}) {
    for (bool blockedBySprint : {false, true}) {
      EnemyLifecycleTestAccess firing{activity, 12}, quiet{activity, 12};
      firing.prepareOccludedNoiseScene();
      quiet.prepareOccludedNoiseScene();
      GameplayInput input;
      input.aimWorldPosition = Vec2{1020, 1500}; // fire away from all test actors
      input.sprint = blockedBySprint;
      quiet.combatTick(input, 0.01F);
      input.fireJustPressed = true;
      firing.combatTick(input, 0.01F);
      EXPECT_EQ(firing.shooting().shotFiredLastUpdate(), !blockedBySprint);
      for (std::size_t i = 0; i < firing.ids.size(); ++i) {
        const auto &heard = firing.actors()[i];
        const auto &unheard = quiet.actors()[i];
        EXPECT_FLOAT_EQ(heard.position().x, unheard.position().x);
        EXPECT_FLOAT_EQ(heard.position().y, unheard.position().y);
        if (blockedBySprint) {
          EXPECT_EQ(heard.checkpoint(), unheard.checkpoint());
        } else {
          const auto known = heard.lastKnownTargetPosition();
          ASSERT_TRUE(known);
          const auto size = firing.daily ? firing.daily->playerSize() : Vec2{40, 52};
          EXPECT_FLOAT_EQ(known->x, 1000 + size.x * 0.5F);
          EXPECT_FLOAT_EQ(known->y, 2000 + size.y * 0.5F);
        }
      }
      input.fireJustPressed = false;
      firing.combatTick(input, 0.01F);
      EXPECT_FALSE(firing.shooting().shotFiredLastUpdate());
      EXPECT_EQ(firing.actors().size(), 3U);
    }
  }
}

template <class T>
concept CanEraseActors = requires(T &v) { v.erase(v.begin()); };
static_assert(!CanEraseActors<EnemyRoster<>>);
static_assert(!std::is_assignable_v<EnemyLifecycle &, const EnemyLifecycle &>);
static_assert(!std::is_assignable_v<EnemyLifecycle &, EnemyLifecycle &&>);
static_assert(!std::is_convertible_v<EnemyRoster<> &, std::vector<Enemy> &>);

TEST(EnemyLifecycleOwnership, MoveAndCopyDoNotBindCleanupToPreviousOwner) {
  EnemyRoster<Vec2> source;
  source.spawn(Enemy{{10, 10}, {20, 20}, {}, 3, 7}, {40, 40});
  auto copy = source;
  auto moved = std::move(copy);
  const std::array<CombatTargetId, 1> ids{7};
  const auto facts = moved.removeForObjective(ids);
  ASSERT_EQ(facts.size(), 1U);
  EXPECT_EQ(facts.front().reason, EnemyRemovalReason::ObjectiveReached);
  EXPECT_FALSE(moved.hasState(7));
  EXPECT_TRUE(source.hasState(7));
  EXPECT_NE(source.find(7), nullptr);
  EXPECT_TRUE(moved.removeDead().empty());
}

TEST(EnemyLifecycleOwnership,
     RestoredTombstoneCannotRegisterAgainOrProduceAnotherDeath) {
  EnemyRoster<> restored;
  restored.restoreRetiredIdentity(7);
  EXPECT_THROW(restored.spawn(Enemy{{}, {20, 20}, {}, 3, 7}),
               std::invalid_argument);
  EXPECT_TRUE(restored.removeDead().empty());
  EXPECT_TRUE(restored.empty());
  restored.spawn(Enemy{{}, {20, 20}, {}, 3, 8});
  EXPECT_THROW(restored.restoreRetiredIdentity(8), std::invalid_argument);
  EXPECT_NE(restored.find(8), nullptr);
}

TEST(EnemyLifecycleOwnership, NewActivityCannotInheritPreviousTargetIntent) {
  EnemyLifecycleTestAccess fixture{Activity::Daily};
  fixture.queueHit(fixture.ids[1], 3);
  auto old = fixture.shooting().checkpoint();
  ASSERT_EQ(old.flights.size(), 1U);
  old.flights.front().aimedTarget = fixture.ids[1];
  old.flights.front().aimedRegion =
      static_cast<std::uint32_t>(HitRegion::Torso);
  ASSERT_TRUE(fixture.shooting().restoreCheckpoint(old));
  BaseDefenseSnapshot seed;
  seed.eventId = "lifecycle-new-activity";
  seed.siegeSequence = 1;
  seed.seed = 12345;
  seed.frozenPopulation = 8;
  const auto candidate = fixture.daily->prepareBaseDefenseSnapshot(seed, publishedContentRegistry().enemyCombatDefinition(ordinaryInfectedDefinitionId()));
  ASSERT_TRUE(candidate);
  EXPECT_TRUE(candidate->shooting.flights.empty());
  // Preparing is a query, not a successful activity transition or a rollback.
  EXPECT_EQ(fixture.shooting().checkpoint(), old);
}

static void checkDailyPersistence(bool rejectFirstSave) {
  struct TemporarySave {
    std::filesystem::path path =
        std::filesystem::temp_directory_path() /
        ("raidline-lifecycle-" +
         std::to_string(
             std::chrono::steady_clock::now().time_since_epoch().count()));
    ~TemporarySave() {
      std::error_code ec;
      std::filesystem::remove_all(path, ec);
    }
  } save;
  GameSession session;
  session.configurePersistence(save.path);
  ASSERT_TRUE(session.startNewProfile("lifecycle-roundtrip"));
  EnemyLifecycleTestAccess fixture{Activity::Daily};
  fixture.daily = std::make_unique<BaseWorld>();
  static_cast<void>(session.updateBaseWorld(*fixture.daily, {}, 0.000001F));
  ASSERT_GE(fixture.actors().size(), 2U);
  const auto victim = fixture.actors()[0].combatTargetId();
  const auto survivor = fixture.actors()[1].combatTargetId();
  const Vec2 before = fixture.actors()[1].position();
  const auto count = fixture.actors().size();
  const auto originalHealth = fixture.actors()[0].health();
  const auto fingerprint = profileStateFingerprint(session.profile());
  const auto obstruction = save.path / "profile.tmp.json";
  if (rejectFirstSave)
    ASSERT_TRUE(std::filesystem::create_directory(obstruction));
  fixture.queueHit(victim, 100);
  static_cast<void>(session.updateBaseWorld(*fixture.daily, {}, 0.000001F));
  if (rejectFirstSave) {
    EXPECT_EQ(profileStateFingerprint(session.profile()), fingerprint);
    ASSERT_EQ(fixture.actors().size(), count);
    ASSERT_NE(fixture.actors().find(victim), nullptr);
    EXPECT_EQ(fixture.actors().find(victim)->health(), originalHealth);
    ASSERT_TRUE(std::filesystem::remove(obstruction));
    fixture.queueHit(victim, 100);
    static_cast<void>(session.updateBaseWorld(*fixture.daily, {}, 0.000001F));
  }
  ASSERT_EQ(fixture.actors().size(), count - 1);
  const RegionalBaseSiteDefinitionId site{fixture.daily->siteDefinitionId()};
  const auto &saved = session.profile().homePerimeter.sites.at(site).enemies;
  const auto dead =
      std::find_if(saved.begin(), saved.end(),
                   [victim](const auto &e) { return e.localId == victim; });
  ASSERT_NE(dead, saved.end());
  EXPECT_EQ(dead->health, 0);
  for (int i = 0; i < 3; ++i)
    static_cast<void>(session.updateBaseWorld(*fixture.daily, {}, 0.000001F));
  ASSERT_NE(fixture.actors().find(survivor), nullptr);
  EXPECT_FLOAT_EQ(fixture.actors().find(survivor)->position().x, before.x);
  EXPECT_FLOAT_EQ(fixture.actors().find(survivor)->position().y, before.y);

  GameSession resumed;
  resumed.configurePersistence(save.path);
  ASSERT_TRUE(resumed.continueProfile()) << resumed.persistenceMessage();
  BaseWorld restored;
  static_cast<void>(resumed.updateBaseWorld(restored, {}, 0.000001F));
  EXPECT_EQ(restored.perimeterEnemies().size(), count - 1);
  EXPECT_TRUE(std::none_of(
      restored.perimeterEnemies().begin(), restored.perimeterEnemies().end(),
      [victim](const Enemy &e) { return e.combatTargetId() == victim; }));
  const auto live = std::find_if(
      restored.perimeterEnemies().begin(), restored.perimeterEnemies().end(),
      [survivor](const Enemy &e) { return e.combatTargetId() == survivor; });
  ASSERT_NE(live, restored.perimeterEnemies().end());
  EXPECT_FLOAT_EQ(live->position().x, before.x);
  EXPECT_FLOAT_EQ(live->position().y, before.y);
}

TEST(EnemyLifecycleSession,
     DailyKillPersistsWithoutResurrectionOrSurvivorRewind) {
  checkDailyPersistence(false);
}

TEST(EnemyLifecycleSession,
     FailedDailySaveRestoresConsistentlyAndRetryPersistsDeath) {
  checkDailyPersistence(true);
}

namespace {
struct BoundarySave {
  std::filesystem::path path = std::filesystem::temp_directory_path() /
      ("raidline-boundary-" + std::to_string(
          std::chrono::steady_clock::now().time_since_epoch().count()));
  ~BoundarySave() {
    std::error_code ignored;
    std::filesystem::remove_all(path, ignored);
  }
};

void queueBaseFlight(BaseWorld &world) {
  ASSERT_FALSE(world.perimeterEnemies().empty());
  const auto &enemy = world.perimeterEnemies().front();
  auto &shooting = EnemyLifecycleTestAccess::baseShooting(world);
  auto state = shooting.checkpoint();
  LogicalFlightCheckpoint flight;
  flight.id = state.nextShotId++;
  flight.origin = flight.position = checkpointPoint(
      {enemy.position().x + 25, enemy.position().y + 25});
  flight.direction = {1, 0};
  flight.impact = {flight.origin[0] + 100, flight.origin[1]};
  flight.speed = 6000;
  flight.extent = 1;
  flight.maximumDistance = 100;
  flight.damage = 100;
  flight.tracerLifetime = 0.05F;
  flight.aimedTarget = enemy.combatTargetId();
  flight.aimedRegion = static_cast<std::uint32_t>(HitRegion::Torso);
  state.flights.push_back(flight);
  ASSERT_TRUE(shooting.restoreCheckpoint(state));
}

void expectSameRoster(const BaseWorld &actual, const BaseWorld &expected) {
  const auto a = actual.perimeterEnemySnapshots();
  const auto b = expected.perimeterEnemySnapshots();
  ASSERT_EQ(a.size(), b.size());
  for (std::size_t i = 0; i < a.size(); ++i) {
    EXPECT_EQ(a[i].localId, b[i].localId);
    EXPECT_EQ(a[i].health, b[i].health);
    EXPECT_FLOAT_EQ(a[i].position.x, b[i].position.x);
    EXPECT_FLOAT_EQ(a[i].position.y, b[i].position.y);
  }
}
} // namespace

TEST(CombatRuntimeBoundary, SameProcessProfileLoadMatchesFreshProcess) {
  for (const bool newProfile : {false, true}) {
    SCOPED_TRACE(newProfile);
    BoundarySave save;
    GameFlow reused;
    reused.configurePersistence(save.path);
    ASSERT_TRUE(reused.startNewGame("boundary-first"));
    const ItemDefinitionId rifleDefinition{"item.weapon.rifle_basic"};
    const auto &records = reused.gameSession().profile().assets.records();
    const auto rifle = std::find_if(records.begin(), records.end(), [&](const auto &record) {
      return record.second.definitionId == rifleDefinition;
    });
    ASSERT_NE(rifle, records.end());
    ASSERT_TRUE(reused.gameSession().executeProfileInventory(
        InventoryEquipCommand{rifle->first, EquipmentSlotKind::PrimaryWeapon}, "boundary-equip").succeeded);
    reused.updateBase({}, 0.000001F);
    const auto initialCount = reused.baseWorld().perimeterEnemies().size();
    ASSERT_GT(initialCount, 1U);
    queueBaseFlight(reused.baseWorld());
    // Simulate transient combat newer than the durable Profile. The reload,
    // not a normal same-cycle synchronization, must import authoritative state.
    static_cast<void>(reused.baseWorld().update({}, 0.000001F));
    ASSERT_EQ(reused.baseWorld().perimeterEnemies().size(), initialCount - 1);
    queueBaseFlight(reused.baseWorld());
    ASSERT_TRUE(reused.returnToMainMenu());
    if (newProfile) ASSERT_TRUE(reused.startNewGame("boundary-second"));
    else ASSERT_TRUE(reused.continueGame());
    EXPECT_TRUE(EnemyLifecycleTestAccess::baseShooting(reused.baseWorld()).logicalBallistics().empty());
    reused.updateBase({}, 0.000001F);
    GameFlow fresh;
    fresh.configurePersistence(save.path);
    ASSERT_TRUE(fresh.continueGame());
    fresh.updateBase({}, 0.000001F);
    expectSameRoster(reused.baseWorld(), fresh.baseWorld());
    EXPECT_EQ(reused.baseWorld().perimeterEnemies().size(), initialCount);
    if (!newProfile)
      EXPECT_EQ(EnemyLifecycleTestAccess::baseShooting(reused.baseWorld()).checkpoint().weaponDamage,
                publishedContentRegistry().item(rifleDefinition).weaponUse->baseDamage);
  }
}

TEST(CombatRuntimeBoundary, DailyCycleReplacementDropsOldFlightsButSameCycleDoesNot) {
  EnemyLifecycleTestAccess fixture{Activity::Daily};
  fixture.queueHit(fixture.ids[1], 100);
  const auto before = fixture.shooting().checkpoint();
  fixture.daily->configureHomePerimeter(&fixture.perimeter);
  EXPECT_EQ(fixture.shooting().checkpoint(), before);
  ++fixture.perimeter.cycleIndex;
  fixture.daily->configureHomePerimeter(&fixture.perimeter);
  EXPECT_TRUE(fixture.shooting().logicalBallistics().empty());
  fixture.tick();
  EXPECT_EQ(fixture.actors().size(), 3U);
  EXPECT_EQ(fixture.deaths(), 0U);
}

TEST(CombatRuntimeBoundary, RelocationCannotResumeOldSpatialShooting) {
  for (const bool atGate : {false, true}) {
    SCOPED_TRACE(atGate);
    EnemyLifecycleTestAccess fixture{Activity::Daily};
    fixture.aimAndFire(25, false);
    ASSERT_TRUE(fixture.shooting().shotFiredLastUpdate());
    fixture.queueHit(fixture.ids[1], 100);
    if (atGate) fixture.daily->resetAtRaidGate();
    else fixture.daily->resetAtMedicalPoint();
    EXPECT_TRUE(fixture.shooting().logicalBallistics().empty());
    EXPECT_FALSE(fixture.shooting().shotFiredLastUpdate());
    EXPECT_TRUE(fixture.shooting().hitResultsLastUpdate().empty());
    fixture.tick();
    EXPECT_EQ(fixture.actors().size(), 3U);
  }
}

TEST(CombatRuntimeBoundary, AcceptedDeployClearsDailyShotsRejectedDeployPreservesThem) {
  for (const bool reject : {false, true}) {
    SCOPED_TRACE(reject);
    GameFlow flow;
    ASSERT_TRUE(flow.startNewGame("boundary-deploy"));
    flow.updateBase({}, 0.000001F);
    queueBaseFlight(flow.baseWorld());
    const auto before = EnemyLifecycleTestAccess::baseShooting(flow.baseWorld()).checkpoint();
    const auto fingerprint = profileStateFingerprint(flow.gameSession().profile());
    EXPECT_EQ(flow.deploy(MapDefinitionId{reject ? "map.unknown" : "map.v0.test"}), !reject);
    if (reject) {
      EXPECT_EQ(EnemyLifecycleTestAccess::baseShooting(flow.baseWorld()).checkpoint(), before);
      EXPECT_EQ(profileStateFingerprint(flow.gameSession().profile()), fingerprint);
    } else {
      EXPECT_TRUE(EnemyLifecycleTestAccess::baseShooting(flow.baseWorld()).logicalBallistics().empty());
      EXPECT_TRUE(flow.gameSession().world().logicalBallistics().empty());
    }
  }
}

TEST(CombatRuntimeBoundary, SpatialClearDropsFactsButPreservesWeaponAndShotSequence) {
  EnemyLifecycleTestAccess fixture{Activity::Daily};
  fixture.aimAndFire(25, false);
  ASSERT_TRUE(fixture.shooting().shotFiredLastUpdate());
  auto expected = fixture.shooting().checkpoint();
  expected.flights.clear();
  fixture.daily->clearSpatialCombatState();
  EXPECT_FALSE(fixture.shooting().shotFiredLastUpdate());
  EXPECT_EQ(fixture.shooting().checkpoint(), expected);
  EXPECT_TRUE(fixture.shooting().shotPresentationSnapshots().empty());
  EXPECT_TRUE(fixture.shooting().shotFeedbackPresentationSnapshots().empty());
  EXPECT_TRUE(fixture.shooting().hitResultsLastUpdate().empty());
  EXPECT_FALSE(fixture.daily->perimeterDamageObservation());
}

TEST(CombatRuntimeBoundary, FailedProfileLoadOrCreationDoesNotInvalidateLiveWorld) {
  BoundarySave save;
  GameFlow flow;
  ASSERT_TRUE(flow.startNewGame("boundary-reject"));
  flow.updateBase({}, 0.000001F);
  queueBaseFlight(flow.baseWorld());
  const auto before = EnemyLifecycleTestAccess::baseShooting(flow.baseWorld()).checkpoint();
  const auto count = flow.baseWorld().perimeterEnemies().size();
  const auto fingerprint = profileStateFingerprint(flow.gameSession().profile());
  ASSERT_TRUE(flow.returnToMainMenu());
  flow.configurePersistence(save.path);
  EXPECT_FALSE(flow.continueGame()); // no save exists
  ASSERT_TRUE(std::filesystem::create_directories(save.path / "profile.tmp.json"));
  EXPECT_FALSE(flow.startNewGame("boundary-rejected-new"));
  EXPECT_EQ(flow.state(), GameFlowState::MainMenu);
  EXPECT_EQ(profileStateFingerprint(flow.gameSession().profile()), fingerprint);
  EXPECT_EQ(EnemyLifecycleTestAccess::baseShooting(flow.baseWorld()).checkpoint(), before);
  EXPECT_EQ(flow.baseWorld().perimeterEnemies().size(), count);
}

TEST(CombatRuntimeBoundary, DefenseRoundTripRetainsDailyRosterAndRestoresOnlyOwnFlight) {
  GameFlow flow;
  ASSERT_TRUE(flow.startNewGame("boundary-defense"));
  flow.updateBase({}, 0.000001F);
  BaseWorld dailyBefore = flow.baseWorld();
  queueBaseFlight(flow.baseWorld());
  ASSERT_TRUE(flow.gameSession().triggerDeveloperBaseSiegeWarning());
  ASSERT_TRUE(flow.gameSession().startBaseRealtimeDefense(flow.baseWorld()));
  ASSERT_TRUE(flow.baseWorld().baseDefenseCheckpoint()->shooting.flights.empty());
  expectSameRoster(flow.baseWorld(), dailyBefore);

  BaseInput fire;
  fire.firePressed = fire.fireJustPressed = true;
  const auto position = flow.baseWorld().playerPosition();
  fire.aimWorldPosition = Vec2{position.x + 500, position.y};
  static_cast<void>(flow.baseWorld().update(fire, 0.000001F));
  auto saved = flow.baseWorld().baseDefenseCheckpoint();
  ASSERT_TRUE(saved);
  ASSERT_FALSE(saved->shooting.flights.empty());
  const auto expected = baseDefenseCheckpointHash(*saved);
  ASSERT_TRUE(flow.baseWorld().resumeBaseDefense(*saved));
  EXPECT_EQ(baseDefenseCheckpointHash(*flow.baseWorld().baseDefenseCheckpoint()), expected);
  auto invalid = *saved;
  invalid.layoutIdentity = "not-this-layout";
  EXPECT_FALSE(flow.baseWorld().resumeBaseDefense(invalid));
  EXPECT_EQ(baseDefenseCheckpointHash(*flow.baseWorld().baseDefenseCheckpoint()), expected);
  expectSameRoster(flow.baseWorld(), dailyBefore);
  ASSERT_TRUE(flow.gameSession().abandonBaseRealtimeDefense());
  EXPECT_FALSE(flow.baseWorld().baseDefenseActive());
  EXPECT_TRUE(EnemyLifecycleTestAccess::baseShooting(flow.baseWorld()).logicalBallistics().empty());
  EXPECT_FALSE(flow.baseWorld().shotFiredLastUpdate());
  expectSameRoster(flow.baseWorld(), dailyBefore);
}

TEST(CombatRuntimeBoundary, EmptyInitialShootingCheckpointCannotInheritPriorFlights) {
  EnemyLifecycleTestAccess fixture{Activity::Daily};
  fixture.queueHit(fixture.ids[0], 100);
  auto expected = fixture.shooting().checkpoint();
  expected.flights.clear();
  ASSERT_TRUE(fixture.shooting().restoreCheckpoint(WorldShootingCheckpoint{}));
  EXPECT_EQ(fixture.shooting().checkpoint(), expected);
}

TEST(CombatRuntimeBoundary, PortalRoundTripsKeepSpaceRosterAndDiscardPreviousTargetFlights) {
  RaidWorldConfig config;
  config.worldSize = {800, 600};
  config.playerSpawn = {100, 100};
  config.extractionPoint = {{650, 450}, {100, 100}};
  config.initialEnemies = {EnemySpawn{{620, 100}, {50, 50}, 12}};
  RaidInteriorWorldConfig interior;
  interior.id = RaidSpaceDefinitionId{"raid_space.test.boundary"};
  interior.displayName = "Boundary test";
  interior.worldSize = {480, 360};
  interior.exteriorEntrance = {{80, 80}, {100, 100}};
  interior.exteriorReturn = {100, 100};
  interior.interiorSpawn = {80, 80};
  interior.interiorExit = {{60, 60}, {120, 120}};
  interior.initialEnemies = {EnemySpawn{{300, 80}, {50, 50}, 12}};
  config.interiors.push_back(interior);
  EnemyLifecycleTestAccess fixture{Activity::Raid};
  fixture.raid = std::make_unique<GameplayWorld>(std::move(config));
  const auto outdoor = fixture.actors().front().checkpoint();
  fixture.queueHit(outdoor.id, 100);
  GameplayInput portal;
  portal.interactJustPressed = true;
  fixture.raid->update(portal, 0);
  ASSERT_FALSE(fixture.raid->inOutdoorRaidSpace());
  EXPECT_TRUE(fixture.shooting().logicalBallistics().empty());
  const auto inside = fixture.actors().front().checkpoint();
  fixture.queueHit(inside.id, 100);
  fixture.raid->update(portal, 0);
  ASSERT_TRUE(fixture.raid->inOutdoorRaidSpace());
  EXPECT_TRUE(fixture.shooting().logicalBallistics().empty());
  EXPECT_EQ(fixture.actors().front().checkpoint(), outdoor);
  fixture.raid->update(portal, 0);
  // The active-space AI may reselect its role on re-entry, even at dt=0.
  // Identity, damage and position must survive; this is not an AI freeze rule.
  EXPECT_EQ(fixture.actors().front().checkpoint().id, inside.id);
  EXPECT_EQ(fixture.actors().front().checkpoint().health, inside.health);
  EXPECT_EQ(fixture.actors().front().checkpoint().position, inside.position);
  fixture.hit(inside.id, 100);
  ASSERT_TRUE(fixture.actors().empty());
  EXPECT_FALSE(fixture.attached(inside.id));
  fixture.raid->update(portal, 0);
  EXPECT_EQ(fixture.actors().front().checkpoint(), outdoor);
  fixture.raid->update(portal, 0);
  EXPECT_TRUE(fixture.actors().empty());
  EXPECT_FALSE(fixture.attached(inside.id));
  EXPECT_TRUE(fixture.shooting().hitResultsLastUpdate().empty());
}
