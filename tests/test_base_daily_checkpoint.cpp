#include "alpha_content_ids.h"
#include "game_flow.h"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <future>
#include <gtest/gtest.h>
#include <iostream>
#include <mutex>
#include <thread>

// Arrange real delayed flights; the frame still uses the production Daily
// adapter, damage resolver and EnemyLifecycle, not a mock HP mutation.
struct EnemyLifecycleTestAccess {
  static void queueHit(BaseWorld &world, int damage = 1) {
    const auto &enemy = world.perimeterEnemies().front();
    auto state = world.shooting_.checkpoint();
    LogicalFlightCheckpoint flight;
    flight.id = state.nextShotId++;
    flight.origin = flight.position =
        checkpointPoint({enemy.position().x + enemy.size().x / 2,
                         enemy.position().y + enemy.size().y / 2});
    flight.direction = {1, 0};
    flight.impact = {flight.origin[0] + 100, flight.origin[1]};
    flight.speed = 6000;
    flight.extent = 1;
    flight.maximumDistance = 100;
    flight.damage = damage;
    flight.tracerLifetime = 0.05F;
    flight.aimedTarget = enemy.combatTargetId();
    flight.aimedRegion = static_cast<std::uint32_t>(HitRegion::Torso);
    state.flights.push_back(flight);
    ASSERT_TRUE(world.shooting_.restoreCheckpoint(state));
  }
};

struct BaseDailyCheckpointTestAccess {
  static void useStore(GameSession &session,
                       BaseDefenseCheckpointWriter::WriteFunction write) {
    session.baseDailyWriter_ =
        std::make_unique<BaseDefenseCheckpointWriter>(std::move(write));
  }
};

namespace {
using Clock = std::chrono::steady_clock;
using namespace std::chrono_literals;
struct TemporarySave {
  std::filesystem::path path =
      std::filesystem::temp_directory_path() /
      ("raidline-daily-checkpoint-" +
       std::to_string(Clock::now().time_since_epoch().count()));
  ~TemporarySave() {
    std::error_code ignored;
    std::filesystem::remove_all(path, ignored);
  }
};

struct DailyFixture : ::testing::Test {
  TemporarySave save;
  GameFlow flow;

  void SetUp() override {
    flow.configurePersistence(save.path);
    ASSERT_TRUE(flow.startNewGame("daily-checkpoint", false));
    flow.updateBase({}, 0.000001F);
    ASSERT_FALSE(flow.baseWorld().perimeterEnemies().empty());
  }
  GameSession &session() { return flow.gameSession(); }
  BaseWorld &world() { return flow.baseWorld(); }
  void hit(int damage = 1, float dt = 0.000001F) {
    EnemyLifecycleTestAccess::queueHit(world(), damage);
    flow.updateBase({}, dt);
    ASSERT_EQ(world().hitResultsLastUpdate().size(), 1U);
  }
  void due() {
    for (int i = 0; i < 3; ++i)
      flow.updateBase({}, 0.1F);
  }
  ProfileState saved() {
    auto result = SaveRepository{save.path}.load(publishedContentRegistry());
    if (!result.profile)
      throw std::runtime_error(result.message);
    return std::move(*result.profile);
  }
  AssetInstanceId asset(const ItemDefinitionId &definition) {
    for (const auto &[id, record] : session().profile().assets.records())
      if (record.definitionId == definition)
        return id;
    return 0;
  }
  void expectSaved() {
    EXPECT_EQ(profileStateFingerprint(saved()),
              profileStateFingerprint(session().profile()));
  }
};

// Blocks a real SaveRepository call behind a deterministic test-only barrier.
// This proves submission never waits on the filesystem, even on a very fast CI
// disk.
struct HeldStore {
  SaveRepository repository;
  std::mutex mutex;
  std::condition_variable changed;
  bool entered{}, released{};
  std::thread::id thread;
  explicit HeldStore(std::filesystem::path path)
      : repository{std::move(path)} {}
  auto operation() {
    return [this](const ProfileState &profile, std::string_view version,
                  SaveWriteMetrics *metrics) {
      {
        std::unique_lock lock{mutex};
        entered = true;
        thread = std::this_thread::get_id();
        changed.notify_all();
        if (!changed.wait_for(lock, 5s, [&] { return released; }))
          return SaveWriteResult{false, "test barrier timed out"};
      }
      return repository.save(profile, version, metrics);
    };
  }
  bool waitEntered() {
    std::unique_lock lock{mutex};
    return changed.wait_for(lock, 5s, [&] { return entered; });
  }
  void release() {
    {
      std::lock_guard lock{mutex};
      released = true;
    }
    changed.notify_all();
  }
};

struct ReleaseStore {
  HeldStore &store;
  GameSession &session;
  ~ReleaseStore() {
    store.release();
    // Destroy the writer before its captured test store, even after ASSERT.
    static_cast<void>(session.retryBaseDailySave());
    BaseDailyCheckpointTestAccess::useStore(
        session, [](const ProfileState &, std::string_view,
                    SaveWriteMetrics *) { return SaveWriteResult{true, {}}; });
  }
};

TEST_F(DailyFixture,
       NonlethalAndLethalFactsAreBatchedThenRoundTripWithoutRespawn) {
  const auto original = profileStateFingerprint(saved());
  const auto count = world().perimeterEnemies().size();
  const auto id = world().perimeterEnemies().front().combatTargetId();
  const auto hp = world().perimeterEnemies().front().health();
  const auto ammunitionId = asset(alpha_content::ammunition);
  const auto unrelatedParticipant = reinterpret_cast<std::uintptr_t>(
      session().profile().assets.find(ammunitionId));
  for (int i = 0; i < 4; ++i)
    hit();
  EXPECT_EQ(reinterpret_cast<std::uintptr_t>(session().profile().assets.find(ammunitionId)),
            unrelatedParticipant); // a hit must not copy/replace the Registry
  EXPECT_EQ(world().perimeterEnemies().front().health(), hp - 4);
  EXPECT_EQ(profileStateFingerprint(saved()), original);
  EXPECT_EQ(session().baseDailyCheckpointStatus().requestedGeneration, 0U);
  hit(100);
  EXPECT_EQ(world().perimeterEnemies().size(), count - 1);
  ASSERT_TRUE(
      flow.returnToMainMenu()); // explicit durable barrier, no App required
  expectSaved();
  ASSERT_TRUE(flow.continueGame());
  flow.updateBase({}, 0.000001F);
  EXPECT_EQ(world().perimeterEnemies().size(), count - 1);
  EXPECT_TRUE(std::none_of(
      world().perimeterEnemies().begin(), world().perimeterEnemies().end(),
      [&](const auto &enemy) { return enemy.combatTargetId() == id; }));
}

TEST_F(DailyFixture,
       BlockedRealStoreNeverRunsOnFrameThreadAndCoalescesNewerHits) {
  HeldStore store{save.path};
  BaseDailyCheckpointTestAccess::useStore(session(), store.operation());
  ReleaseStore release{store, session()};
  const auto original = profileStateFingerprint(saved());
  hit();
  due();
  ASSERT_TRUE(store.waitEntered());
  EXPECT_NE(store.thread, std::this_thread::get_id());
  EXPECT_TRUE(session().baseDailyCheckpointStatus().outstanding());
  hit();
  due();
  hit();
  due();
  EXPECT_EQ(profileStateFingerprint(saved()), original);
  EXPECT_GE(session().baseDailyCheckpointStatus().coalescedRequests, 1U);
  store.release();
  ASSERT_TRUE(session().checkpointWorldClock());
  EXPECT_FALSE(session().baseDailyCheckpointStatus().outstanding());
  expectSaved();
}

TEST_F(DailyFixture,
       SaveFailureFreezesActorsAndBlocksTransactionsDeployAndQuitUntilRetry) {
  const auto obstruction = save.path / "profile.tmp.json";
  ASSERT_TRUE(std::filesystem::create_directory(obstruction));
  hit(100);
  EXPECT_FALSE(session().checkpointWorldClock());
  ASSERT_TRUE(session().baseDailySaveBlocked());
  const auto accepted = profileStateFingerprint(session().profile());
  const auto positions = world().perimeterEnemySnapshots();
  BaseInput input;
  input.firePressed = input.fireJustPressed = true;
  for (int i = 0; i < 20; ++i) {
    flow.updateBase(input, 0.1F);
    session().advanceBaseWorldClock(0.1F);
  }
  EXPECT_EQ(profileStateFingerprint(session().profile()), accepted);
  EXPECT_EQ(world().perimeterEnemySnapshots(), positions);
  const auto weapon = asset(alpha_content::rifle);
  EXPECT_FALSE(
      session()
          .executeProfileInventory(
              InventoryEquipCommand{weapon, EquipmentSlotKind::PrimaryWeapon},
              "blocked-equip")
          .succeeded);
  EXPECT_FALSE(session().deployAlpha(1001));
  EXPECT_FALSE(session().continueProfile());
  EXPECT_FALSE(session().startNewProfile("must-not-replace"));
  EXPECT_FALSE(flow.returnToMainMenu());
  EXPECT_EQ(profileStateFingerprint(session().profile()), accepted);
  ASSERT_TRUE(std::filesystem::remove(obstruction));
  ASSERT_TRUE(session().retryBaseDailySave());
  EXPECT_FALSE(session().baseDailySaveBlocked());
  expectSaved();
}

TEST_F(DailyFixture,
       ExcessiveWriteLagPausesAndRecoversWithoutRebuildingEnemies) {
  HeldStore store{save.path};
  BaseDailyCheckpointTestAccess::useStore(session(), store.operation());
  ReleaseStore release{store, session()};
  hit(100);
  due();
  ASSERT_TRUE(store.waitEntered());
  const auto positions = world().perimeterEnemySnapshots();
  const auto accepted = profileStateFingerprint(session().profile());
  std::this_thread::sleep_for(1050ms);
  flow.updateBase({}, 0.1F);
  ASSERT_TRUE(session().baseDailySaveBlocked());
  EXPECT_EQ(world().perimeterEnemySnapshots(), positions);
  EXPECT_EQ(profileStateFingerprint(session().profile()), accepted);
  store.release();
  ASSERT_TRUE(session().checkpointWorldClock());
  flow.updateBase({}, 0.000001F);
  EXPECT_FALSE(session().baseDailySaveBlocked());
  EXPECT_EQ(world().perimeterEnemySnapshots().size(), positions.size());
  expectSaved();
}

TEST_F(DailyFixture, InventoryTransactionDrainsOlderWriteBeforeSavingNewState) {
  HeldStore store{save.path};
  BaseDailyCheckpointTestAccess::useStore(session(), store.operation());
  ReleaseStore release{store, session()};
  hit();
  due();
  ASSERT_TRUE(store.waitEntered());
  hit(100);
  const auto weapon = asset(alpha_content::rifle);
  auto command = std::async(std::launch::async, [&] {
    return session().executeProfileInventory(
        InventoryEquipCommand{weapon, EquipmentSlotKind::PrimaryWeapon},
        "ordered-equip");
  });
  // The main thread does not touch Session while the command owns it.
  EXPECT_EQ(command.wait_for(20ms), std::future_status::timeout);
  store.release();
  ASSERT_TRUE(command.get().succeeded);
  EXPECT_EQ(session().baseDailyCheckpointStatus().requestedGeneration, 0U);
  expectSaved();
}

TEST_F(DailyFixture,
       RejectedInventoryIsNeverSubmittedAndDoesNotLosePendingDeath) {
  hit(100);
  due();
  const auto accepted = profileStateFingerprint(session().profile());
  EXPECT_FALSE(
      session()
          .executeProfileInventory(
              InventoryEquipCommand{0, EquipmentSlotKind::PrimaryWeapon},
              "invalid-equip")
          .succeeded);
  EXPECT_EQ(profileStateFingerprint(session().profile()), accepted);
  ASSERT_TRUE(session().checkpointWorldClock());
  expectSaved();
}

TEST_F(DailyFixture,
       DeployAndAbandonedRaidRecoveryCannotBeOverwrittenByDailyCheckpoint) {
  hit(100);
  due();
  const auto enemies = session().profile().homePerimeter;
  ASSERT_TRUE(session().deployAlpha(4125));
  EXPECT_EQ(session().baseDailyCheckpointStatus().requestedGeneration, 0U);
  GameSession reopened;
  reopened.configurePersistence(save.path);
  ASSERT_TRUE(reopened.continueProfile());
  EXPECT_FALSE(reopened.profile().pendingRaid);
  EXPECT_EQ(reopened.profile().homePerimeter, enemies);
}

TEST_F(DailyFixture, DailyToDefenseTransfersSingleWriterOwnership) {
  hit(100);
  due();
  const auto enemies = session().profile().homePerimeter;
  ASSERT_TRUE(session().triggerDeveloperBaseSiegeWarning());
  ASSERT_TRUE(session().startBaseRealtimeDefense(world()));
  EXPECT_EQ(session().baseDailyCheckpointStatus().requestedGeneration, 0U);
  ASSERT_TRUE(session().checkpointWorldClock());
  const auto snapshot = saved();
  EXPECT_TRUE(snapshot.activeBaseDefense);
  EXPECT_EQ(snapshot.homePerimeter, enemies);
}

TEST_F(DailyFixture,
       FireMissAndActualHitSaveOneConsistentAmmunitionCheckpoint) {
  const auto rifle = asset(alpha_content::rifle);
  const auto magazine = asset(alpha_content::magazine);
  ASSERT_TRUE(
      session()
          .executeProfileInventory(
              InventoryEquipCommand{rifle, EquipmentSlotKind::PrimaryWeapon},
              "equip")
          .succeeded);
  ASSERT_TRUE(session()
                  .executeProfileWeaponAmmo(
                      LoadMagazineCommand{magazine,
                                          asset(alpha_content::ammunition), 12},
                      "load")
                  .succeeded);
  ASSERT_TRUE(
      session()
          .executeProfileWeaponAmmo(
              InstallMagazineAndChamberCommand{rifle, magazine}, "install")
          .succeeded);
  const auto rounds =
      session().profile().assets.find(magazine)->magazineRounds.size();
  BaseInput input;
  input.firePressed = input.fireJustPressed = true;
  input.aimWorldPosition =
      Vec2{world().playerPosition().x + 400, world().playerPosition().y};
  flow.updateBase(input, 0.01F);
  ASSERT_TRUE(world().shotFiredLastUpdate());
  EXPECT_EQ(session().profile().assets.find(magazine)->magazineRounds.size(),
            rounds - 1);
  hit(100);
  ASSERT_TRUE(flow.returnToMainMenu());
  expectSaved();
  EXPECT_EQ(saved().assets.find(magazine)->magazineRounds.size(), rounds - 1);
}

TEST_F(DailyFixture,
       NewProfileAndRepositorySwitchCannotReceiveAnOlderQueuedProfile) {
  hit();
  due();
  ASSERT_TRUE(session().startNewProfile("second-profile"));
  EXPECT_EQ(saved().profileId, "second-profile");
  EXPECT_EQ(session().baseDailyCheckpointStatus().requestedGeneration, 0U);
  flow.updateBase({}, 0.000001F);
  hit();
  due();
  TemporarySave second;
  session().configurePersistence(second.path);
  EXPECT_EQ(saved().profileId, "second-profile");
  ASSERT_TRUE(session().startNewProfile("third-profile"));
  EXPECT_EQ(SaveRepository{second.path}
                .load(publishedContentRegistry())
                .profile->profileId,
            "third-profile");
}

TEST_F(DailyFixture,
       RepeatedRealDiskHitsStayWithinFrameBudgetIncludingCheckpointCopies) {
  for (const std::size_t assetCount : {0U, 1000U}) {
    SCOPED_TRACE(assetCount);
    if (assetCount > 0) {
      ASSERT_TRUE(flow.returnToMainMenu());
      ASSERT_TRUE(flow.startNewGame("daily-large-checkpoint", false));
      flow.updateBase({}, 0.000001F);
      ASSERT_TRUE(flow.returnToMainMenu());
      auto profile = session().profile();
      while (profile.assets.records().size() < assetCount) {
        const float offset =
            static_cast<float>(profile.assets.records().size());
        static_cast<void>(profile.assets.create(
            publishedContentRegistry().item(alpha_content::ammunition),
            BaseGroundAssetLocation{
                RegionalBaseSiteDefinitionId{world().siteDefinitionId()},
                {5500 + offset, 5100}},
            1));
      }
      ASSERT_TRUE(
          SaveRepository{save.path}
              .save(profile, publishedContentRegistry().contentVersion())
              .succeeded);
      ASSERT_TRUE(flow.continueGame());
      flow.updateBase({}, 0.000001F);
    }
    std::vector<double> timings;
    const auto count = world().perimeterEnemies().size();
    unsigned hits{}, deaths{};
    while (!world().perimeterEnemies().empty()) {
      EnemyLifecycleTestAccess::queueHit(world());
      const auto started = Clock::now();
      flow.updateBase({}, 0.03F);
      timings.push_back(
          std::chrono::duration<double, std::milli>{Clock::now() - started}
              .count());
      ASSERT_EQ(world().hitResultsLastUpdate().size(), 1U);
      ++hits;
      deaths += world().hitResultsLastUpdate().front().targetKilled ? 1U : 0U;
      ASSERT_FALSE(session().baseDailySaveBlocked());
    }
    ASSERT_TRUE(session().checkpointWorldClock());
    expectSaved();
    ASSERT_GE(hits, 70U);
    EXPECT_EQ(deaths, count);
    std::sort(timings.begin(), timings.end());
    const auto p95 = timings[(timings.size() * 95 + 99) / 100 - 1];
    const auto status = session().baseDailyCheckpointStatus();
    std::cout << "Daily real-disk assets="
              << session().profile().assets.records().size()
              << " hit frames=" << hits
              << " p50=" << timings[timings.size() / 2] << "ms p95=" << p95
              << "ms max=" << timings.back()
              << "ms checkpoint-copy=" << status.lastCopyMilliseconds
              << "ms worker-save=" << status.lastWriteMetrics.totalMilliseconds
              << "ms\n";
    EXPECT_LT(p95, 8.0);
    EXPECT_LT(timings.back(), 25.0);
    EXPECT_GT(status.durableGeneration, 1U);
    EXPECT_LT(status.requestedGeneration, hits / 2);
  }
}
} // namespace
