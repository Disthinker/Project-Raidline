#include <gtest/gtest.h>

#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <future>
#include <limits>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <type_traits>
#include <vector>

#include "base_defense_checkpoint_writer.h"

namespace {
using namespace std::chrono_literals;

// Explicit barriers make concurrency assertions independent of scheduler speed.
struct ControlledStore {
  std::mutex mutex;
  std::condition_variable changed;
  std::size_t releasedThrough{};
  bool failFirst{};
  std::vector<std::uint32_t> currencies;
  std::vector<std::string> versions;
  std::vector<std::uint64_t> durableFingerprints;

  SaveWriteResult write(const ProfileState &profile, std::string_view version,
                        SaveWriteMetrics *metrics) {
    std::unique_lock lock{mutex};
    currencies.push_back(profile.currency);
    versions.emplace_back(version);
    const auto attempt = currencies.size();
    changed.notify_all();
    if (!changed.wait_for(lock, 5s,
                          [&] { return releasedThrough >= attempt; })) {
      return {false, "test store barrier timed out"};
    }
    if (metrics) {
      metrics->serializationMilliseconds = 0.1;
      metrics->commitMilliseconds = 0.2;
    }
    durableFingerprints.push_back(profileStateFingerprint(profile));
    return attempt == 1 && failFirst
               ? SaveWriteResult{false, "simulated disk failure"}
               : SaveWriteResult{true, {}};
  }

  bool waitStarted(std::size_t count) {
    std::unique_lock lock{mutex};
    return changed.wait_for(lock, 5s,
                            [&] { return currencies.size() >= count; });
  }

  void release(std::size_t count = std::numeric_limits<std::size_t>::max()) {
    {
      const std::lock_guard lock{mutex};
      releasedThrough = count;
    }
    changed.notify_all();
  }

  BaseDefenseCheckpointWriter::WriteFunction operation() {
    return [this](const ProfileState &profile, std::string_view version,
                  SaveWriteMetrics *metrics) {
      return write(profile, version, metrics);
    };
  }
};

struct ReleaseOnExit {
  ControlledStore &store;
  ~ReleaseOnExit() { store.release(); }
};

ProfileState profileForQueue(std::uint32_t currency) {
  ProfileState profile;
  profile.profileId = "checkpoint-queue-test";
  profile.currency = currency;
  return profile;
}

AssetRecord copyTestAsset(AssetInstanceId id) {
  AssetRecord asset;
  asset.instanceId = id;
  asset.definitionId = ItemDefinitionId{"item.copy_test.definition"};
  asset.quantity = 3;
  asset.orientation = ItemOrientation::Degrees90;
  asset.remainingCharges = 2;
  asset.currentMaximumDurability = 81;
  asset.currentDurability = 57;
  asset.reliefBatchId = "copy-test-relief-batch";
  asset.magazineRounds = {
      {ItemDefinitionId{"ammo.copy_test.first"}, "first-round-relief"},
      {ItemDefinitionId{"ammo.copy_test.second"}, std::nullopt}};
  asset.chamberedRound = MagazineRoundRecord{
      ItemDefinitionId{"ammo.copy_test.chamber"}, "chamber-relief"};
  asset.location = RaidGroundAssetLocation{"copy-test-raid-identity", 23U};
  return asset;
}

TEST(BaseDefenseCheckpointWriterTest,
     RegistryCopyPreservesAllValuesAndReusesOnlyMatchingIds) {
  static_assert(std::is_nothrow_move_constructible_v<AssetRegistry>);
  static_assert(std::is_nothrow_move_assignable_v<AssetRegistry>);
  ProfileState source = profileForQueue(7);
  ProfileState destination = source;
  for (const AssetInstanceId id : {2U, 4U, 7U})
    ASSERT_TRUE(source.assets.insertLoaded(copyTestAsset(id)));
  for (const AssetInstanceId id : {1U, 2U, 5U, 7U, 9U})
    ASSERT_TRUE(destination.assets.insertLoaded(copyTestAsset(id)));
  source.assets.setNextAssetIdForLoad(100);
  destination.assets.setNextAssetIdForLoad(200);
  source.revision = 99;
  source.currency = 271;
  source.medicalStatus.painkillerRemainingMs = 700;
  source.committedTransactions.insert("preserve-all-profile-fields");
  const auto original = profileStateFingerprint(source);
  const AssetRecord *reused2 = destination.assets.find(2);
  const AssetRecord *reused7 = destination.assets.find(7);
  destination = source;
  EXPECT_EQ(profileStateFingerprint(destination), original);
  EXPECT_EQ(profileStateFingerprint(source), original);
  EXPECT_EQ(destination.assets.nextAssetId(), 100U);
  EXPECT_EQ(destination.assets.records().size(), 3U);
  EXPECT_EQ(destination.assets.find(2), reused2);
  EXPECT_EQ(destination.assets.find(7), reused7);
  EXPECT_EQ(destination.assets.find(1), nullptr);
  EXPECT_EQ(destination.assets.find(5), nullptr);
  EXPECT_EQ(destination.assets.find(9), nullptr);
  ASSERT_NE(destination.assets.find(4), nullptr);
  EXPECT_NE(destination.assets.find(2), source.assets.find(2));
  EXPECT_NE(destination.assets.find(2)->magazineRounds.data(),
            source.assets.find(2)->magazineRounds.data());
  source.assets.findMutable(2)->magazineRounds.front().definitionId =
      ItemDefinitionId{"ammo.changed_after_copy"};
  source.assets.findMutable(2)->chamberedRound->reliefBatchId = "changed";
  source.assets.findMutable(2)->location = InstalledMagazineLocation{987U};
  source.assets.setNextAssetIdForLoad(101);
  EXPECT_EQ(profileStateFingerprint(destination), original);
}

TEST(BaseDefenseCheckpointWriterTest,
     RegistryCopyHandlesSelfAssignmentEmptySourceAndExactHighWater) {
  AssetRegistry source;
  ASSERT_TRUE(source.insertLoaded(copyTestAsset(5)));
  source.setNextAssetIdForLoad(77);
  const AssetRecord *same = source.find(5);
  source = static_cast<const AssetRegistry &>(source);
  EXPECT_EQ(source.find(5), same);
  EXPECT_EQ(source.nextAssetId(), 77U);
  AssetRegistry destination{source};
  EXPECT_NE(destination.find(5), source.find(5));
  AssetRegistry empty;
  empty.setNextAssetIdForLoad(6);
  destination = empty;
  EXPECT_TRUE(destination.records().empty());
  EXPECT_EQ(destination.nextAssetId(), 6U);
  destination = source;
  EXPECT_EQ(destination.records().size(), 1U);
  EXPECT_EQ(destination.nextAssetId(), 77U);
  AssetRegistry moved{std::move(destination)};
  EXPECT_EQ(moved.records().size(), 1U);
  EXPECT_EQ(moved.nextAssetId(), 77U);
}

TEST(BaseDefenseCheckpointWriterTest,
     CompletedWriteReusesItsProfileAndAssetNodesWithoutCoalescing) {
  std::vector<const ProfileState *> profileAddresses;
  std::vector<const AssetRecord *> assetAddresses;
  std::vector<std::uint64_t> fingerprints;
  BaseDefenseCheckpointWriter writer{
      [&](const ProfileState &profile, std::string_view,
          SaveWriteMetrics *) -> SaveWriteResult {
        profileAddresses.push_back(&profile);
        assetAddresses.push_back(profile.assets.find(2));
        fingerprints.push_back(profileStateFingerprint(profile));
        return {true, {}};
      }};
  ProfileState profile = profileForQueue(1);
  ASSERT_TRUE(profile.assets.insertLoaded(copyTestAsset(2)));
  profile.assets.setNextAssetIdForLoad(10);
  for (std::uint32_t generation = 1; generation <= 3; ++generation) {
    profile.currency = generation;
    profile.assets.findMutable(2)->remainingCharges = generation;
    static_cast<void>(writer.request(profile, "v1"));
    ASSERT_TRUE(writer.flush().succeeded);
    EXPECT_EQ(fingerprints.back(), profileStateFingerprint(profile));
  }
  ASSERT_EQ(profileAddresses.size(), 3U);
  EXPECT_EQ(profileAddresses[0], profileAddresses[1]);
  EXPECT_EQ(profileAddresses[1], profileAddresses[2]);
  EXPECT_EQ(assetAddresses[0], assetAddresses[1]);
  EXPECT_EQ(assetAddresses[1], assetAddresses[2]);
  EXPECT_EQ(writer.snapshot().coalescedRequests, 0U);
  EXPECT_EQ(writer.snapshot().durableGeneration, 3U);
}

TEST(BaseDefenseCheckpointWriterTest,
     InFlightDeepAssetsStayFrozenUntilStoreCompletes) {
  ControlledStore store;
  BaseDefenseCheckpointWriter writer{store.operation()};
  ReleaseOnExit release{store};
  ProfileState profile = profileForQueue(1);
  ASSERT_TRUE(profile.assets.insertLoaded(copyTestAsset(2)));
  profile.assets.setNextAssetIdForLoad(10);
  const auto expected = profileStateFingerprint(profile);
  static_cast<void>(writer.request(profile, "v1"));
  ASSERT_TRUE(store.waitStarted(1));
  profile.assets.findMutable(2)->magazineRounds.clear();
  profile.assets.findMutable(2)->chamberedRound.reset();
  profile.assets.findMutable(2)->location = InstalledMagazineLocation{44U};
  profile.assets.setNextAssetIdForLoad(20);
  store.release();
  ASSERT_TRUE(writer.flush().succeeded);
  ASSERT_EQ(store.durableFingerprints.size(), 1U);
  EXPECT_EQ(store.durableFingerprints[0], expected);
}

TEST(BaseDefenseCheckpointWriterTest, EmptyBarrierDoesNotCreateAWrite) {
  ControlledStore store;
  BaseDefenseCheckpointWriter writer{store.operation()};
  EXPECT_TRUE(writer.flush().succeeded);
  const auto status = writer.snapshot();
  EXPECT_FALSE(status.outstanding());
  EXPECT_FALSE(status.pending);
  EXPECT_FALSE(status.failed);
  EXPECT_EQ(status.inFlightGeneration, 0U);
  EXPECT_DOUBLE_EQ(status.durabilityLagMilliseconds, 0.0);
  EXPECT_FALSE(writer.retry());
  EXPECT_TRUE(store.currencies.empty());
}

TEST(BaseDefenseCheckpointWriterTest,
     KeepsOneImmutableInFlightAndOnlyNewestPending) {
  ControlledStore store;
  BaseDefenseCheckpointWriter writer{store.operation()};
  ReleaseOnExit release{store};
  ProfileState profile = profileForQueue(1);
  EXPECT_EQ(writer.request(profile, "v1"), 1U);
  ASSERT_TRUE(store.waitStarted(1));
  profile.currency = 2;
  EXPECT_EQ(writer.request(profile, "v2"), 2U);
  profile.currency = 3;
  EXPECT_EQ(writer.request(profile, "v3"), 3U);
  profile.currency = 4;
  EXPECT_EQ(writer.request(profile, "v4"), 4U);
  profile.currency = 999;
  const auto queued = writer.snapshot();
  EXPECT_EQ(queued.inFlightGeneration, 1U);
  EXPECT_TRUE(queued.pending);
  EXPECT_EQ(queued.coalescedRequests, 2U);
  EXPECT_EQ(queued.durableGeneration, 0U);
  store.release();
  ASSERT_TRUE(writer.flush().succeeded);
  EXPECT_EQ(store.currencies, (std::vector<std::uint32_t>{1, 4}));
  EXPECT_EQ(store.versions, (std::vector<std::string>{"v1", "v4"}));
  const auto saved = writer.snapshot();
  EXPECT_EQ(saved.durableGeneration, 4U);
  EXPECT_FALSE(saved.outstanding());
  EXPECT_FALSE(saved.pending);
  EXPECT_EQ(saved.inFlightGeneration, 0U);
  EXPECT_GE(saved.lastCopyMilliseconds, 0.0);
  EXPECT_DOUBLE_EQ(saved.lastWriteMetrics.serializationMilliseconds, 0.1);
}

TEST(BaseDefenseCheckpointWriterTest,
     BarrierWaitsForActualCommitNotQueueAdmission) {
  ControlledStore store;
  BaseDefenseCheckpointWriter writer{store.operation()};
  ReleaseOnExit release{store};
  static_cast<void>(writer.request(profileForQueue(7), "v1"));
  ASSERT_TRUE(store.waitStarted(1));
  auto barrier = std::async(std::launch::async, [&] { return writer.flush(); });
  EXPECT_EQ(barrier.wait_for(0ms), std::future_status::timeout);
  EXPECT_TRUE(writer.snapshot().outstanding());
  store.release();
  EXPECT_TRUE(barrier.get().succeeded);
  EXPECT_EQ(writer.snapshot().durableGeneration, 1U);
}

TEST(BaseDefenseCheckpointWriterTest,
     FailedWritePausesAndRetryUsesNewestCoherentCandidate) {
  ControlledStore store;
  store.failFirst = true;
  BaseDefenseCheckpointWriter writer{store.operation()};
  ReleaseOnExit release{store};
  static_cast<void>(writer.request(profileForQueue(1), "v1"));
  ASSERT_TRUE(store.waitStarted(1));
  static_cast<void>(writer.request(profileForQueue(2), "v2"));
  store.release(1);
  const auto failure = writer.flush();
  EXPECT_FALSE(failure.succeeded);
  EXPECT_EQ(failure.message, "simulated disk failure");
  EXPECT_TRUE(writer.snapshot().failed);
  EXPECT_EQ(writer.snapshot().durableGeneration, 0U);
  static_cast<void>(writer.request(profileForQueue(3), "v3"));
  EXPECT_TRUE(writer.snapshot().failed);
  {
    const std::lock_guard lock{store.mutex};
    EXPECT_EQ(store.currencies.size(), 1U);
  }
  EXPECT_TRUE(writer.retry());
  store.release();
  ASSERT_TRUE(writer.flush().succeeded);
  EXPECT_EQ(store.currencies, (std::vector<std::uint32_t>{1, 3}));
  EXPECT_EQ(writer.snapshot().durableGeneration, 3U);
  EXPECT_FALSE(writer.snapshot().failed);
  EXPECT_TRUE(writer.snapshot().message.empty());
}

TEST(BaseDefenseCheckpointWriterTest,
     RetryRetainsFailedSnapshotWhenNoNewerCandidateExists) {
  ControlledStore store;
  store.failFirst = true;
  store.release();
  BaseDefenseCheckpointWriter writer{store.operation()};
  static_cast<void>(writer.request(profileForQueue(42), "v1"));
  EXPECT_FALSE(writer.flush().succeeded);
  EXPECT_TRUE(writer.snapshot().pending);
  EXPECT_TRUE(writer.retry());
  EXPECT_TRUE(writer.flush().succeeded);
  EXPECT_EQ(store.currencies, (std::vector<std::uint32_t>{42, 42}));
  EXPECT_EQ(writer.snapshot().requestedGeneration, 1U);
  EXPECT_EQ(writer.snapshot().durableGeneration, 1U);
}

TEST(BaseDefenseCheckpointWriterTest,
     ExceptionsBecomeRetryableFailuresAndDestructionDoesNotRetry) {
  std::size_t calls{};
  {
    BaseDefenseCheckpointWriter writer{
        [&](const ProfileState &, std::string_view,
            SaveWriteMetrics *) -> SaveWriteResult {
          ++calls;
          throw std::runtime_error{"store exception"};
        }};
    static_cast<void>(writer.request(profileForQueue(42), "v1"));
    const auto failure = writer.flush();
    EXPECT_FALSE(failure.succeeded);
    EXPECT_EQ(failure.message, "store exception");
    EXPECT_TRUE(writer.snapshot().failed);
  }
  EXPECT_EQ(calls, 1U);
}

TEST(BaseDefenseCheckpointWriterTest,
     DestructionDrainsAcceptedWorkBeforeRepositoryIsReleased) {
  ControlledStore store;
  store.release();
  {
    BaseDefenseCheckpointWriter writer{store.operation()};
    static_cast<void>(writer.request(profileForQueue(9), "v1"));
  }
  EXPECT_EQ(store.currencies, (std::vector<std::uint32_t>{9}));
}

TEST(BaseDefenseCheckpointWriterTest,
     NewPendingSnapshotDoesNotResetOutstandingDurabilityAge) {
  ControlledStore store;
  BaseDefenseCheckpointWriter writer{store.operation()};
  ReleaseOnExit release{store};
  static_cast<void>(writer.request(profileForQueue(1), "v1"));
  ASSERT_TRUE(store.waitStarted(1));
  std::this_thread::sleep_for(15ms);
  const double previousAge = writer.snapshot().durabilityLagMilliseconds;
  EXPECT_GT(previousAge, 0.0);
  static_cast<void>(writer.request(profileForQueue(2), "v2"));
  EXPECT_GE(writer.snapshot().durabilityLagMilliseconds, previousAge);
  store.release();
  EXPECT_TRUE(writer.flush().succeeded);
  EXPECT_DOUBLE_EQ(writer.snapshot().durabilityLagMilliseconds, 0.0);
}

TEST(BaseDefenseCheckpointWriterTest,
     RejectsCrossProfileRequestWithoutChangingQueue) {
  ControlledStore store;
  store.release();
  BaseDefenseCheckpointWriter writer{store.operation()};
  static_cast<void>(writer.request(profileForQueue(1), "v1"));
  EXPECT_TRUE(writer.flush().succeeded);
  ProfileState other = profileForQueue(2);
  other.profileId = "different-profile";
  EXPECT_THROW(static_cast<void>(writer.request(other, "v2")),
               std::invalid_argument);
  EXPECT_EQ(writer.snapshot().requestedGeneration, 1U);
  EXPECT_EQ(writer.snapshot().durableGeneration, 1U);
}

TEST(BaseDefenseCheckpointWriterTest,
     RejectedReplacementPreservesAcceptedPendingAndGeneration) {
  ControlledStore store;
  BaseDefenseCheckpointWriter writer{store.operation()};
  ReleaseOnExit release{store};
  static_cast<void>(writer.request(profileForQueue(1), "v1"));
  ASSERT_TRUE(store.waitStarted(1));
  static_cast<void>(writer.request(profileForQueue(2), "v2"));
  static_cast<void>(writer.request(profileForQueue(3), "v3"));
  // The replacement copies into the retired v2 buffer. A subsequent guard
  // rejection must not damage the accepted v3 candidate or its generation.
  ProfileState other = profileForQueue(99);
  other.profileId = "different-profile";
  EXPECT_THROW(static_cast<void>(writer.request(other, "rejected")),
               std::invalid_argument);
  EXPECT_EQ(writer.snapshot().requestedGeneration, 3U);
  EXPECT_EQ(writer.snapshot().coalescedRequests, 1U);
  EXPECT_TRUE(writer.snapshot().pending);
  store.release();
  ASSERT_TRUE(writer.flush().succeeded);
  EXPECT_EQ(store.currencies, (std::vector<std::uint32_t>{1, 3}));
  EXPECT_EQ(store.versions, (std::vector<std::string>{"v1", "v3"}));
}

class TemporaryCheckpointDirectory {
public:
  TemporaryCheckpointDirectory()
      : path_{std::filesystem::temp_directory_path() /
              ("raidline-defense-checkpoint-" +
               std::to_string(std::chrono::steady_clock::now()
                                  .time_since_epoch()
                                  .count()))} {}
  ~TemporaryCheckpointDirectory() {
    std::error_code error;
    std::filesystem::remove_all(path_, error);
  }
  std::filesystem::path path_;
};

TEST(BaseDefenseCheckpointWriterTest,
     AtomicRepositoryPersistsFullProfileWithMeasuredWorkerStages) {
  TemporaryCheckpointDirectory directory;
  const ContentRegistry &content = publishedContentRegistry();
  SaveRepository repository{directory.path_};
  ProfileState profile = makeNewAlphaProfile("checkpoint-round-trip", content);
  const auto originalFingerprint = profileStateFingerprint(profile);
  BaseDefenseCheckpointWriter writer{repository};
  EXPECT_EQ(writer.request(profile, content.contentVersion()), 1U);
  ASSERT_TRUE(writer.flush().succeeded);
  EXPECT_EQ(profileStateFingerprint(profile), originalFingerprint);
  const auto restored = repository.load(content);
  ASSERT_TRUE(restored.profile) << restored.message;
  EXPECT_EQ(profileStateFingerprint(*restored.profile), originalFingerprint);
  const auto status = writer.snapshot();
  EXPECT_GT(status.lastWriteMetrics.serializationMilliseconds, 0.0);
  EXPECT_GT(status.lastWriteMetrics.commitMilliseconds, 0.0);
  EXPECT_GE(status.lastWriteMetrics.totalMilliseconds,
            status.lastWriteMetrics.serializationMilliseconds);
}
} // namespace
