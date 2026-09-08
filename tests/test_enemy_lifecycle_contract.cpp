#include "base_world.h"
#include "game_session.h"
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

  explicit EnemyLifecycleTestAccess(Activity kind) : activity(kind) {
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
                                     3,
                                     3});
      daily->configureHomePerimeter(&perimeter);
      daily->playerPosition_ = {100, 100};
      daily->movementBlockers_.clear();
      daily->movementBlockerIndex_ =
          RaidSpaceBlockerIndex::build(daily->worldSize(), {});
    } else if (kind == Activity::Raid) {
      std::vector<EnemySpawn> spawns;
      for (auto p : positions)
        spawns.push_back(EnemySpawn{p, {50, 50}, 3});
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
      const auto prepared = BaseDefenseRuntime::prepare(s, {});
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
            Enemy{positions[i], {50, 50}, {}, 3, ids[i]},
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
};

class EnemyLifecycleContract : public testing::TestWithParam<Activity> {};

TEST_P(EnemyLifecycleContract, NonLethalPreservesIdentityAndAttachedState) {
  EnemyLifecycleTestAccess fixture{GetParam()};
  fixture.hit(fixture.ids[1], 1);
  ASSERT_EQ(fixture.actors().size(), 3U);
  ASSERT_NE(fixture.actors().find(fixture.ids[1]), nullptr);
  EXPECT_EQ(fixture.actors().find(fixture.ids[1])->health(), 2);
  EXPECT_TRUE(fixture.attached(fixture.ids[1]));
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
  const auto candidate = fixture.daily->prepareBaseDefenseSnapshot(seed);
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
    EXPECT_EQ(fixture.actors().find(victim)->health(), 3);
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
