#include "alpha_content_ids.h"
#include "game_flow.h"
#include "test_temp_directory.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <gtest/gtest.h>
#include <mutex>
#include <thread>

struct InventoryCheckpointTestAccess {
  static void store(GameSession &session, BaseDefenseCheckpointWriter::WriteFunction write) {
    session.baseDailyWriter_ = std::make_unique<BaseDefenseCheckpointWriter>(std::move(write));
  }
  static void defenseStore(GameSession &session, BaseDefenseCheckpointWriter::WriteFunction write) {
    session.baseDefenseWriter_ = std::make_unique<BaseDefenseCheckpointWriter>(std::move(write));
  }
  static void clearStores(GameSession &session) {
    session.baseDailyWriter_.reset();
    session.baseDefenseWriter_.reset();
  }
  static AssetInstanceId createWorldAsset(GameSession &session, AssetInstanceId weapon) {
    const auto site = session.profile_.homeFounding.established
        ? session.profile_.regionalOperations.technologyCore.baseSiteDefinitionId : kFoundingRegion;
    const auto id = session.profile_.assets.create(publishedContentRegistry().item(alpha_content::ammunition),
        BaseGroundAssetLocation{site, {100, 100}}, 1);
    session.profile_.currentHealth = 71;
    session.profile_.assets.findMutable(weapon)->currentDurability = 53;
    static_cast<void>(advanceWorldClock(session.profile_.worldClock, 7));
    ++session.profile_.revision;
    session.worldClockDirty_ = true;
    return id;
  }
  static void worldFacts(GameSession &session, AssetInstanceId id) {
    session.profile_.currentHealth = 71;
    session.profile_.assets.findMutable(id)->currentDurability = 53;
    session.worldClockDirty_ = true;
  }
};

namespace {
using namespace std::chrono_literals;
struct Store {
  std::mutex mutex;
  std::condition_variable changed;
  bool entered{}, released{};
  unsigned calls{};
  std::atomic<bool> failAfterFirst{}, failAll{};
  SaveRepository repository;
  explicit Store(std::filesystem::path path) : repository(std::move(path)) {}
  SaveWriteResult write(const ProfileState &profile, std::string_view version, SaveWriteMetrics *metrics) {
    {
      std::unique_lock lock(mutex);
      ++calls; entered = true; changed.notify_all();
      if (!changed.wait_for(lock, 5s, [&] { return released; })) return {false, "test barrier timeout"};
      if (failAll || (failAfterFirst && calls > 1)) return {false, "deterministic storage failure"};
    }
    return repository.save(profile, version, metrics);
  }
  void release() { { std::lock_guard lock(mutex); released = true; } changed.notify_all(); }
  bool waitEntered() { std::unique_lock lock(mutex); return changed.wait_for(lock, 5s, [&]{ return entered; }); }
};

struct InventoryUiPersistence : ::testing::Test {
  raidline::test::TemporaryDirectory directory;
  GameFlow flow;
  std::shared_ptr<Store> store;
  AssetInstanceId weapon{};
  StoredAssetLocation original;
  void SetUp() override {
    directory.claim("raidline-inventory-ui-", std::string_view{::testing::UnitTest::GetInstance()->current_test_info()->name()}.substr(0, 24));
    flow.configurePersistence(directory.path());
    ASSERT_TRUE(flow.startNewGame("inventory-ui", false));
    for (const auto &[id, asset] : session().profile().assets.records())
      if (asset.definitionId == alpha_content::rifle) {
        weapon = id;
        ASSERT_TRUE(std::holds_alternative<StoredAssetLocation>(asset.location));
        original = std::get<StoredAssetLocation>(asset.location);
        break;
      }
    ASSERT_NE(weapon, 0U);
    store = std::make_shared<Store>(directory.path());
    InventoryCheckpointTestAccess::store(session(), [owned = store](const ProfileState &p, std::string_view v, SaveWriteMetrics *m) {
      return owned->write(p, v, m);
    });
  }
  void TearDown() override {
    if (store) store->release();
    // Ensure the bounded writer no longer runs before the temp directory dies.
    InventoryCheckpointTestAccess::clearStores(session());
  }
  GameSession &session() { return flow.gameSession(); }
  InventoryUiSubmission equip(std::string tx) {
    return session().requestProfileInventory(InventoryEquipCommand{weapon, EquipmentSlotKind::PrimaryWeapon}, std::move(tx));
  }
  InventoryUiSubmission putBack(std::string tx) {
    return session().requestProfileInventory(InventoryMoveCommand{weapon, 0, original}, std::move(tx));
  }
  void attachStore(bool defense) {
    store = std::make_shared<Store>(directory.path());
    auto write = [owned = store](const ProfileState &p, std::string_view v, SaveWriteMetrics *m) {
      return owned->write(p, v, m);
    };
    if (defense) InventoryCheckpointTestAccess::defenseStore(session(), std::move(write));
    else InventoryCheckpointTestAccess::store(session(), std::move(write));
  }
  void beginDefense() {
    ASSERT_TRUE(session().triggerDeveloperBaseSiegeWarning()) << session().persistenceMessage();
    ASSERT_TRUE(session().startBaseRealtimeDefense(flow.baseWorld())) << session().persistenceMessage();
    ASSERT_TRUE(session().baseDefenseActive());
    ASSERT_FALSE(session().profile().pendingRaid);
    attachStore(true);
  }
  bool waitSettled() {
    for (unsigned i = 0; i < 5000; ++i) {
      session().updateInventoryPersistence(0);
      const auto status = session().baseDefenseActive() ? session().baseDefenseCheckpointStatus()
                                                       : session().baseDailyCheckpointStatus();
      if (status.failed || (!status.outstanding() && !session().inventoryPersistencePending())) return true;
      std::this_thread::sleep_for(1ms);
    }
    return false;
  }
};

TEST_F(InventoryUiPersistence, ProjectionIsImmediateAndAuthorityWaitsForDurableGeneration) {
  const auto request = equip("one");
  ASSERT_TRUE(request.accepted); ASSERT_TRUE(request.pending); ASSERT_NE(request.requestId, 0U);
  EXPECT_FALSE(session().profile().committedTransactions.contains("one"));
  EXPECT_TRUE(std::holds_alternative<StoredAssetLocation>(session().profile().assets.find(weapon)->location));
  EXPECT_TRUE(std::holds_alternative<EquippedAssetLocation>(session().profileForInventoryPresentation().assets.find(weapon)->location));
  EXPECT_EQ(session().baseDailyCheckpointStatus().requestedGeneration, 0U);
  session().updateInventoryPersistence(.151F);
  ASSERT_TRUE(store->waitEntered());
  EXPECT_TRUE(session().takeInventoryUiCompletions().empty());
  store->release(); ASSERT_TRUE(waitSettled());
  const auto completed = session().takeInventoryUiCompletions();
  ASSERT_EQ(completed.size(), 1U); EXPECT_EQ(completed[0].requestId, request.requestId); EXPECT_TRUE(completed[0].succeeded);
  EXPECT_TRUE(session().profile().committedTransactions.contains("one"));
  const auto saved = SaveRepository{directory.path()}.load(publishedContentRegistry());
  ASSERT_TRUE(saved.profile); EXPECT_TRUE(saved.profile->committedTransactions.contains("one"));
}

TEST_F(InventoryUiPersistence, DebounceAndMaximumFirstDeadlineDoNotLoseOrderedMoves) {
  ASSERT_TRUE(equip("a").pending);
  session().updateInventoryPersistence(.10F);
  ASSERT_TRUE(putBack("b").pending);
  session().updateInventoryPersistence(.10F);
  ASSERT_TRUE(equip("c").pending);
  session().updateInventoryPersistence(.051F);
  ASSERT_TRUE(store->waitEntered());
  EXPECT_EQ(session().baseDailyCheckpointStatus().requestedGeneration, 1U);
  store->release(); ASSERT_TRUE(waitSettled());
  const auto saved = SaveRepository{directory.path()}.load(publishedContentRegistry());
  ASSERT_TRUE(saved.profile);
  for (const auto *tx : {"a", "b", "c"}) EXPECT_TRUE(saved.profile->committedTransactions.contains(tx));
  EXPECT_EQ(session().takeInventoryUiCompletions().size(), 3U);
}

TEST_F(InventoryUiPersistence, CoalescedSuccessorContainsAllRequestsAndPreservesNewWorldFacts) {
  ASSERT_TRUE(equip("first").pending);
  session().requestInventoryPersistenceBarrier();
  ASSERT_TRUE(store->waitEntered());
  ASSERT_TRUE(putBack("second").pending); session().requestInventoryPersistenceBarrier();
  ASSERT_TRUE(equip("third").pending);
  InventoryCheckpointTestAccess::worldFacts(session(), weapon);
  session().requestInventoryPersistenceBarrier();
  EXPECT_FALSE(session().inventoryPersistenceBarrierReady());
  EXPECT_GE(session().baseDailyCheckpointStatus().coalescedRequests, 1U);
  store->release(); ASSERT_TRUE(waitSettled());
  EXPECT_EQ(session().profile().currentHealth, 71);
  EXPECT_EQ(session().profile().assets.find(weapon)->currentDurability, 53U);
  EXPECT_EQ(session().takeInventoryUiCompletions().size(), 3U);
  const auto saved = SaveRepository{directory.path()}.load(publishedContentRegistry());
  ASSERT_TRUE(saved.profile); EXPECT_EQ(saved.profile->currentHealth, 71);
  EXPECT_EQ(saved.profile->assets.find(weapon)->currentDurability, 53U);
  for (const auto *tx : {"first", "second", "third"}) EXPECT_TRUE(saved.profile->committedTransactions.contains(tx));
}

TEST_F(InventoryUiPersistence, FailedSuffixRollsBackPreviewButKeepsDurablePrefixAndRetryCannotResurrectIt) {
  store->failAfterFirst = true;
  const auto first = equip("durable"); ASSERT_TRUE(first.pending);
  session().requestInventoryPersistenceBarrier(); ASSERT_TRUE(store->waitEntered());
  const auto second = putBack("failed"); ASSERT_TRUE(second.pending);
  session().requestInventoryPersistenceBarrier();
  store->release(); ASSERT_TRUE(waitSettled());
  session().updateInventoryPersistence(0);
  EXPECT_FALSE(session().inventoryPersistencePending());
  EXPECT_TRUE(session().profile().committedTransactions.contains("durable"));
  EXPECT_FALSE(session().profile().committedTransactions.contains("failed"));
  const auto completions = session().takeInventoryUiCompletions();
  ASSERT_EQ(completions.size(), 2U); EXPECT_TRUE(completions[0].succeeded); EXPECT_FALSE(completions[1].succeeded);
  EXPECT_FALSE(session().inventoryPersistenceBarrierReady());
  store->failAfterFirst = false;
  ASSERT_TRUE(session().retryBaseDailySave());
  const auto saved = SaveRepository{directory.path()}.load(publishedContentRegistry());
  ASSERT_TRUE(saved.profile); EXPECT_FALSE(saved.profile->committedTransactions.contains("failed"));
  EXPECT_TRUE(std::holds_alternative<EquippedAssetLocation>(saved.profile->assets.find(weapon)->location));
}

TEST_F(InventoryUiPersistence, InvalidMoveNeverQueuesAndBarrierSubmitsWithoutBlockingDisk) {
  const auto rejected = session().requestProfileInventory(InventoryEquipCommand{0, EquipmentSlotKind::PrimaryWeapon}, "invalid");
  EXPECT_FALSE(rejected.accepted); EXPECT_EQ(session().baseDailyCheckpointStatus().requestedGeneration, 0U);
  ASSERT_TRUE(equip("quit-barrier").pending);
  session().requestInventoryPersistenceBarrier(); ASSERT_TRUE(store->waitEntered());
  EXPECT_FALSE(session().inventoryPersistenceBarrierReady());
  store->release(); ASSERT_TRUE(waitSettled());
  EXPECT_TRUE(session().inventoryPersistenceBarrierReady());
}

TEST_F(InventoryUiPersistence, SplitReservesItsStableIdBeforeAnInFlightWorldAssetAndKeepsLatestClockAndWear) {
  const auto &content = publishedContentRegistry();
  AssetInstanceId ammo{};
  for (const auto &[id, asset] : session().profile().assets.records())
    if (asset.definitionId == alpha_content::ammunition && asset.quantity > 3) { ammo = id; break; }
  ASSERT_NE(ammo, 0U);
  const auto sourceQuantity = session().profile().assets.find(ammo)->quantity;
  const auto sourceLocation = session().profile().assets.find(ammo)->location;
  const auto cell = findFirstProfileFit(session().profile(), content, ProfileContainerId::stash(),
      content.item(alpha_content::ammunition), ItemOrientation::Degrees0);
  ASSERT_TRUE(cell);
  const auto splitId = session().profile().assets.nextAssetId();
  const auto startingMinute = session().profile().worldClock.elapsedWorldMinutes;
  const auto request = session().requestProfileInventory(
      InventoryMoveCommand{ammo, 3, {ProfileContainerId::stash(), *cell}}, "provisional-split");
  ASSERT_TRUE(request.accepted); ASSERT_TRUE(request.pending);
  ASSERT_NE(session().profileForInventoryPresentation().assets.find(splitId), nullptr);
  EXPECT_EQ(session().profile().assets.find(splitId), nullptr);
  EXPECT_GT(session().profile().assets.nextAssetId(), splitId);
  session().requestInventoryPersistenceBarrier(); ASSERT_TRUE(store->waitEntered());

  // The main-thread world allocates while the worker still owns the earlier
  // immutable split snapshot. It must not allocate the provisional identity.
  const auto worldId = InventoryCheckpointTestAccess::createWorldAsset(session(), weapon);
  ASSERT_NE(worldId, splitId); EXPECT_GT(worldId, splitId);
  session().requestInventoryPersistenceBarrier();
  const auto &preview = session().profileForInventoryPresentation();
  ASSERT_NE(preview.assets.find(splitId), nullptr); ASSERT_NE(preview.assets.find(worldId), nullptr);
  EXPECT_EQ(preview.assets.find(weapon)->currentDurability, 53U);
  EXPECT_EQ(preview.worldClock.elapsedWorldMinutes, startingMinute + 7);
  store->release(); ASSERT_TRUE(waitSettled());

  EXPECT_EQ(session().profile().assets.find(ammo)->quantity, sourceQuantity - 3);
  EXPECT_EQ(session().profile().assets.find(ammo)->location, sourceLocation);
  ASSERT_NE(session().profile().assets.find(splitId), nullptr); EXPECT_EQ(session().profile().assets.find(splitId)->quantity, 3U);
  ASSERT_NE(session().profile().assets.find(worldId), nullptr); EXPECT_EQ(session().profile().assets.find(worldId)->quantity, 1U);
  EXPECT_EQ(session().profile().assets.find(weapon)->currentDurability, 53U);
  EXPECT_EQ(session().profile().worldClock.elapsedWorldMinutes, startingMinute + 7);
  const auto saved = SaveRepository{directory.path()}.load(content); ASSERT_TRUE(saved.profile) << saved.message;
  ASSERT_NE(saved.profile->assets.find(splitId), nullptr); ASSERT_NE(saved.profile->assets.find(worldId), nullptr);
  EXPECT_EQ(saved.profile->assets.find(splitId)->quantity, 3U); EXPECT_EQ(saved.profile->assets.find(worldId)->quantity, 1U);
  EXPECT_EQ(saved.profile->assets.find(weapon)->currentDurability, 53U);
  EXPECT_EQ(saved.profile->worldClock.elapsedWorldMinutes, startingMinute + 7);
  EXPECT_GT(saved.profile->assets.nextAssetId(), worldId);
  const auto completed = session().takeInventoryUiCompletions(); ASSERT_EQ(completed.size(), 1U);
  EXPECT_EQ(completed.front().requestId, request.requestId); EXPECT_TRUE(completed.front().succeeded);
}

TEST_F(InventoryUiPersistence, RealDefenseUsesItsWriterGenerationsToConfirmACoalescedOrderedPrefix) {
  beginDefense(); ASSERT_TRUE(session().baseDefenseActive());
  const auto event = session().profile().activeBaseDefense->eventId;
  const auto first = equip("defense-first"); ASSERT_TRUE(first.pending);
  session().requestInventoryPersistenceBarrier(); ASSERT_TRUE(store->waitEntered());
  const auto second = putBack("defense-second"); ASSERT_TRUE(second.pending);
  session().requestInventoryPersistenceBarrier();
  const auto third = equip("defense-third"); ASSERT_TRUE(third.pending);
  session().requestInventoryPersistenceBarrier();
  EXPECT_EQ(session().baseDailyCheckpointStatus().requestedGeneration, 0U);
  EXPECT_GE(session().baseDefenseCheckpointStatus().coalescedRequests, 1U);
  EXPECT_FALSE(session().profile().committedTransactions.contains("defense-first"));
  EXPECT_FALSE(session().inventoryPersistenceBarrierReady());
  store->release(); ASSERT_TRUE(waitSettled());
  const auto status = session().baseDefenseCheckpointStatus(); EXPECT_EQ(status.durableGeneration, status.requestedGeneration);
  const auto completed = session().takeInventoryUiCompletions(); ASSERT_EQ(completed.size(), 3U);
  const std::array expectedRequestIds{first.requestId, second.requestId, third.requestId};
  for (std::size_t index = 0; index < completed.size(); ++index) {
    EXPECT_TRUE(completed[index].succeeded);
    EXPECT_EQ(completed[index].requestId, expectedRequestIds[index]);
  }
  const auto saved = SaveRepository{directory.path()}.load(publishedContentRegistry()); ASSERT_TRUE(saved.profile) << saved.message;
  ASSERT_TRUE(saved.profile->activeBaseDefense); EXPECT_EQ(saved.profile->activeBaseDefense->eventId, event);
  EXPECT_FALSE(saved.profile->pendingRaid);
  for (const auto *tx : {"defense-first", "defense-second", "defense-third"}) {
    EXPECT_TRUE(session().profile().committedTransactions.contains(tx)); EXPECT_TRUE(saved.profile->committedTransactions.contains(tx));
  }
  EXPECT_TRUE(std::holds_alternative<EquippedAssetLocation>(saved.profile->assets.find(weapon)->location));
}

TEST_F(InventoryUiPersistence, RealDefenseFailureKeepsItsDurablePrefixAndExplicitRetryDropsOnlyTheFailedSuffix) {
  beginDefense(); ASSERT_TRUE(session().baseDefenseActive()); store->failAfterFirst = true;
  const auto first = equip("defense-durable"); ASSERT_TRUE(first.pending);
  session().requestInventoryPersistenceBarrier(); ASSERT_TRUE(store->waitEntered());
  const auto second = putBack("defense-failed"); ASSERT_TRUE(second.pending);
  session().requestInventoryPersistenceBarrier(); store->release(); ASSERT_TRUE(waitSettled());
  session().updateInventoryPersistence(0);
  EXPECT_TRUE(session().baseDefenseCheckpointStatus().failed); EXPECT_FALSE(session().inventoryPersistencePending());
  EXPECT_TRUE(session().profile().committedTransactions.contains("defense-durable"));
  EXPECT_FALSE(session().profile().committedTransactions.contains("defense-failed"));
  EXPECT_TRUE(std::holds_alternative<EquippedAssetLocation>(session().profile().assets.find(weapon)->location));
  const auto completed = session().takeInventoryUiCompletions(); ASSERT_EQ(completed.size(), 2U);
  EXPECT_EQ(completed[0].requestId, first.requestId); EXPECT_TRUE(completed[0].succeeded);
  EXPECT_EQ(completed[1].requestId, second.requestId); EXPECT_FALSE(completed[1].succeeded);
  EXPECT_FALSE(session().inventoryPersistenceBarrierReady());
  store->failAfterFirst = false; ASSERT_TRUE(session().retryBaseDefenseSave()) << session().persistenceMessage();
  session().updateInventoryPersistence(0); EXPECT_TRUE(session().inventoryPersistenceBarrierReady());
  const auto saved = SaveRepository{directory.path()}.load(publishedContentRegistry()); ASSERT_TRUE(saved.profile) << saved.message;
  ASSERT_TRUE(saved.profile->activeBaseDefense); EXPECT_FALSE(saved.profile->pendingRaid);
  EXPECT_TRUE(saved.profile->committedTransactions.contains("defense-durable"));
  EXPECT_FALSE(saved.profile->committedTransactions.contains("defense-failed"));
  EXPECT_TRUE(std::holds_alternative<EquippedAssetLocation>(saved.profile->assets.find(weapon)->location));
}

TEST_F(InventoryUiPersistence, ContinueForcesAnUnqueuedDefenseMoveDurableBeforeLoadingAndClearsOldGenerations) {
  beginDefense(); ASSERT_TRUE(session().baseDefenseActive());
  const auto first = equip("before-defense-continue"); ASSERT_TRUE(first.pending);
  EXPECT_EQ(session().baseDefenseCheckpointStatus().requestedGeneration, 0U);
  std::atomic<bool> sawWrite{};
  std::jthread releaser([owned = store, &sawWrite] { sawWrite = owned->waitEntered(); owned->release(); });
  const bool continued = session().continueProfile(); releaser.join();
  ASSERT_TRUE(sawWrite); ASSERT_TRUE(continued) << session().persistenceMessage();
  EXPECT_FALSE(session().inventoryPersistencePending()); EXPECT_TRUE(session().takeInventoryUiCompletions().empty());
  EXPECT_TRUE(session().profile().committedTransactions.contains("before-defense-continue"));
  EXPECT_TRUE(std::holds_alternative<EquippedAssetLocation>(session().profile().assets.find(weapon)->location));
  ASSERT_TRUE(session().profile().activeBaseDefense); EXPECT_EQ(session().baseDefenseCheckpointStatus().durableGeneration, 0U);

  // Restore through the same public entry used by GameFlow after loading. A
  // new writer starts at generation zero; the old durable receipt is gone.
  flow.baseWorld().resetCombatForProfileLoad();
  ASSERT_TRUE(session().restoreBaseDefenseRuntime(flow.baseWorld()));
  ASSERT_TRUE(session().restoreBasePlayerRuntime(flow.baseWorld()));
  attachStore(true);
  const auto next = putBack("after-defense-continue"); ASSERT_TRUE(next.pending);
  EXPECT_EQ(next.requestId, 1U); session().updateInventoryPersistence(0);
  EXPECT_FALSE(session().profile().committedTransactions.contains("after-defense-continue"));
  EXPECT_TRUE(session().takeInventoryUiCompletions().empty());
  session().requestInventoryPersistenceBarrier(); ASSERT_TRUE(store->waitEntered());
  EXPECT_FALSE(session().profile().committedTransactions.contains("after-defense-continue"));
  store->release(); ASSERT_TRUE(waitSettled());
  const auto completed = session().takeInventoryUiCompletions(); ASSERT_EQ(completed.size(), 1U);
  EXPECT_EQ(completed.front().requestId, next.requestId); EXPECT_TRUE(completed.front().succeeded);
}

TEST_F(InventoryUiPersistence, NewProfileForcesPendingDailySaveBeforeReplacementAndCannotReuseItsAcknowledgement) {
  const auto first = equip("before-new-profile"); ASSERT_TRUE(first.pending);
  EXPECT_EQ(session().baseDailyCheckpointStatus().requestedGeneration, 0U);
  std::atomic<bool> sawWrite{};
  std::jthread releaser([owned = store, &sawWrite] { sawWrite = owned->waitEntered(); owned->release(); });
  const bool replaced = session().startNewProfile("inventory-ui-replacement", false); releaser.join();
  ASSERT_TRUE(sawWrite); ASSERT_TRUE(replaced) << session().persistenceMessage();
  EXPECT_EQ(session().profile().profileId, "inventory-ui-replacement");
  EXPECT_FALSE(session().profile().committedTransactions.contains("before-new-profile"));
  EXPECT_FALSE(session().inventoryPersistencePending()); EXPECT_TRUE(session().takeInventoryUiCompletions().empty());
  EXPECT_EQ(session().baseDailyCheckpointStatus().requestedGeneration, 0U);
  const auto saved = SaveRepository{directory.path()}.load(publishedContentRegistry()); ASSERT_TRUE(saved.profile) << saved.message;
  EXPECT_EQ(saved.profile->profileId, "inventory-ui-replacement");
  EXPECT_FALSE(saved.profile->committedTransactions.contains("before-new-profile"));
  attachStore(false);
  const auto next = equip("replacement-move"); ASSERT_TRUE(next.pending); EXPECT_EQ(next.requestId, 1U);
  session().updateInventoryPersistence(0);
  EXPECT_FALSE(session().profile().committedTransactions.contains("replacement-move"));
  EXPECT_TRUE(session().takeInventoryUiCompletions().empty());
  session().requestInventoryPersistenceBarrier(); ASSERT_TRUE(store->waitEntered());
  store->release(); ASSERT_TRUE(waitSettled());
  const auto current = SaveRepository{directory.path()}.load(publishedContentRegistry()); ASSERT_TRUE(current.profile) << current.message;
  EXPECT_EQ(current.profile->profileId, "inventory-ui-replacement"); EXPECT_TRUE(current.profile->committedTransactions.contains("replacement-move"));
  EXPECT_FALSE(current.profile->committedTransactions.contains("before-new-profile"));
}

TEST_F(InventoryUiPersistence, FailedPendingSaveBlocksProfileReplacementAndLeavesTheExistingProfileOnDisk) {
  const auto previousId = session().profile().profileId;
  store->failAll = true; ASSERT_TRUE(equip("never-durable").pending);
  session().requestInventoryPersistenceBarrier(); ASSERT_TRUE(store->waitEntered()); store->release(); ASSERT_TRUE(waitSettled());
  session().updateInventoryPersistence(0);
  EXPECT_FALSE(session().startNewProfile("must-not-replace", false));
  EXPECT_EQ(session().profile().profileId, previousId);
  EXPECT_FALSE(session().profile().committedTransactions.contains("never-durable"));
  const auto saved = SaveRepository{directory.path()}.load(publishedContentRegistry()); ASSERT_TRUE(saved.profile) << saved.message;
  EXPECT_EQ(saved.profile->profileId, previousId); EXPECT_FALSE(saved.profile->committedTransactions.contains("never-durable"));
  store->failAll = false; ASSERT_TRUE(session().retryBaseDailySave());
  ASSERT_TRUE(session().startNewProfile("replacement-after-explicit-retry", false));
  EXPECT_EQ(session().profile().profileId, "replacement-after-explicit-retry");
}
TEST_F(InventoryUiPersistence, DefenseTerminalQueuesOnceAndMenuPollingConfirmsItWithoutBlockingTheStore) {
  beginDefense(); ASSERT_TRUE(session().baseDefenseActive());
  EXPECT_FALSE(session().abandonBaseRealtimeDefense());
  ASSERT_TRUE(store->waitEntered());
  const auto generation = session().baseDefenseCheckpointStatus().requestedGeneration;
  ASSERT_EQ(generation, 1U);
  for (unsigned i = 0; i < 10; ++i) {
    session().updateInventoryPersistence(0);
    EXPECT_TRUE(session().baseDefenseActive());
    EXPECT_FALSE(session().abandonBaseRealtimeDefense());
    EXPECT_EQ(session().baseDefenseCheckpointStatus().requestedGeneration, generation);
  }
  store->release();
  for (unsigned i = 0; i < 5000 && session().baseDefenseActive(); ++i) {
    session().updateInventoryPersistence(0);
    std::this_thread::sleep_for(1ms);
  }
  ASSERT_FALSE(session().baseDefenseActive());
  const auto fingerprint = profileStateFingerprint(session().profile());
  session().updateInventoryPersistence(0); session().updateInventoryPersistence(0);
  EXPECT_EQ(profileStateFingerprint(session().profile()), fingerprint);
  const auto saved = SaveRepository{directory.path()}.load(publishedContentRegistry());
  ASSERT_TRUE(saved.profile) << saved.message;
  EXPECT_FALSE(saved.profile->activeBaseDefense);
  EXPECT_EQ(profileStateFingerprint(*saved.profile), fingerprint);
}
} // namespace
