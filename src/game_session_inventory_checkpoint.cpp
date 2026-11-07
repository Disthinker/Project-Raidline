#include "game_session.h"
#include "loadout_progression.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <deque>
#include <limits>
#include <utility>

namespace {
using Clock = std::chrono::steady_clock;
struct InventoryAssetChange {
  AssetInstanceId id{};
  std::optional<AssetRecord> before, after;
};
struct PendingInventoryUiRequest {
  std::uint64_t id{}, firstGeneration{};
  bool defense{};
  std::string transaction;
  std::vector<InventoryAssetChange> changes;
  AssetInstanceId nextAssetId{};
  bool advanceTutorial{};
};

bool advanceInventoryTutorial(ProfileState &profile) {
  if (profile.tutorial != TutorialProgress::PrepareLoadout ||
      profile.revision == std::numeric_limits<ProfileRevision>::max() ||
      !projectLoadoutReadiness(profile, publishedContentRegistry(),
          LoadoutArchetypeDefinitionId{"loadout_archetype.light_scavenger"}).ready()) return false;
  profile.tutorial = TutorialProgress::FindRaidGate;
  ++profile.revision;
  return true;
}

// UI moves change only these inventory fields. Combat wear, cartridges,
// batteries, injuries and world clocks keep their most recent live values.
bool sameInventoryFields(const AssetRecord &a, const AssetRecord &b) {
  return a.definitionId == b.definitionId && a.quantity == b.quantity &&
         a.orientation == b.orientation && a.location == b.location;
}

std::vector<InventoryAssetChange> inventoryChanges(const AssetRegistry &before,
                                                  const AssetRegistry &after) {
  std::vector<InventoryAssetChange> changes;
  for (const auto &[id, asset] : before.records()) {
    const auto *next = after.find(id);
    if (!next || !sameInventoryFields(asset, *next))
      changes.push_back({id, asset, next ? std::optional{*next} : std::nullopt});
  }
  for (const auto &[id, asset] : after.records())
    if (!before.find(id)) changes.push_back({id, std::nullopt, asset});
  return changes;
}

bool applyInventoryChanges(ProfileState &target,
                           const PendingInventoryUiRequest &request) {
  if (target.committedTransactions.contains(request.transaction)) return true;
  if (target.revision == std::numeric_limits<ProfileRevision>::max()) return false;
  // Check all participants before changing any of them.
  for (const auto &change : request.changes) {
    const auto *current = target.assets.find(change.id);
    if (change.before ? (!current || !sameInventoryFields(*current, *change.before))
                      : current != nullptr)
      return false;
  }
  for (const auto &change : request.changes) {
    if (!change.after) {
      static_cast<void>(target.assets.erase(change.id));
    } else if (!change.before) {
      if (!target.assets.insertLoaded(*change.after)) return false;
    } else {
      auto *current = target.assets.findMutable(change.id);
      // A move cannot replace a live gun's state with the state at drag time.
      current->quantity = change.after->quantity;
      current->orientation = change.after->orientation;
      current->location = change.after->location;
    }
  }
  target.assets.setNextAssetIdForLoad(std::max(target.assets.nextAssetId(), request.nextAssetId));
  target.committedTransactions.insert(request.transaction);
  ++target.revision;
  if (request.advanceTutorial && target.tutorial == TutorialProgress::PrepareLoadout &&
      target.revision != std::numeric_limits<ProfileRevision>::max()) {
    target.tutorial = TutorialProgress::FindRaidGate;
    ++target.revision;
  }
  return true;
}

double secondsSince(Clock::time_point time) {
  return std::chrono::duration<double>(Clock::now() - time).count();
}
} // namespace

struct ProfileInventoryPersistenceState {
  std::deque<PendingInventoryUiRequest> requests;
  ProfileState preview;
  std::vector<InventoryUiCompletion> completions;
  std::uint64_t nextRequestId{1};
  Clock::time_point firstAt{}, lastAt{};
  double sinceFirst{}, sinceLast{};
  bool force{}, failed{};
  ProfileRevision previewBaseRevision{};
  AssetInstanceId previewBaseHighWater{};
};

const ProfileState &GameSession::profileForInventoryPresentation() const noexcept {
  return inventoryPersistence_ && !inventoryPersistence_->requests.empty()
             ? inventoryPersistence_->preview : profile_;
}

bool GameSession::inventoryPersistencePending() const noexcept {
  return inventoryPersistence_ && !inventoryPersistence_->requests.empty();
}

std::vector<InventoryUiCompletion> GameSession::takeInventoryUiCompletions() {
  if (!inventoryPersistence_) return {};
  std::vector<InventoryUiCompletion> result;
  result.swap(inventoryPersistence_->completions);
  return result;
}

ProfileState GameSession::inventoryCheckpointCandidate(const ProfileState &current) {
  ProfileState candidate = current;
  if (!inventoryPersistence_) return candidate;
  for (const auto &request : inventoryPersistence_->requests)
    if (!applyInventoryChanges(candidate, request)) {
      // The simulation's structural inventory transitions must use a barrier.
      // Never publish a snapshot that silently omits an earlier accepted move.
      throw std::logic_error{"Pending inventory participants changed before durability"};
    }
  return candidate;
}

void GameSession::noteInventoryCheckpointRequested(std::uint64_t generation,
                                                  bool defense) {
  if (!inventoryPersistence_) return;
  for (auto &request : inventoryPersistence_->requests)
    if (!request.firstGeneration) {
      request.firstGeneration = generation;
      request.defense = defense;
    }
  inventoryPersistence_->force = false;
}

InventoryUiSubmission GameSession::requestProfileInventory(const InventoryCommand &command,
                                                           std::string transaction) {
  if (profileUiCommitPending())
    return {0, false, false, {false, false, DomainErrorCode::IllegalDestination, "PROFILE SAVE PENDING", profile_.revision}};
  if (alphaRaidActive_ || (!saveRepository_ && !baseDailyWriter_ && !baseDefenseWriter_)) {
    auto receipt = executeProfileInventory(command, std::move(transaction));
    return {0, receipt.succeeded, false, std::move(receipt)};
  }
  updateInventoryPersistence(0);
  const auto status = baseDefenseActive() ? baseDefenseCheckpointStatus() : baseDailyCheckpointStatus();
  if (status.failed || baseDailySaveBlocked_ || baseDefenseSaveBlocked_)
    return {0, false, false, {false, false, DomainErrorCode::InvalidProfile,
                            "Save protection is waiting for retry", profile_.revision}};
  if (!inventoryPersistence_) inventoryPersistence_ = std::make_shared<ProfileInventoryPersistenceState>();
  auto &state = *inventoryPersistence_;
  for (const auto &pending : state.requests)
    if (pending.transaction == transaction)
      return {pending.id, true, true, {true, true, DomainErrorCode::None, {}, state.preview.revision}};
  ProfileState candidate = inventoryCheckpointCandidate(profile_);
  AssetRegistry before = candidate.assets;
  const auto receipt = executeInventory(candidate, publishedContentRegistry(), command,
                                       {candidate.revision, transaction});
  if (!receipt.succeeded || receipt.alreadyCommitted)
    return {0, receipt.succeeded, false, receipt};
  if (state.requests.size() >= 128 || state.nextRequestId == std::numeric_limits<std::uint64_t>::max())
    return {0, false, false, {false, false, DomainErrorCode::Capacity,
                            "Wait for the pending inventory saves", profile_.revision}};
  const auto id = state.nextRequestId++;
  auto changes = inventoryChanges(before, candidate.assets);
  const bool tutorial = advanceInventoryTutorial(candidate);
  const bool wasEmpty = state.requests.empty();
  state.requests.push_back({id, 0, baseDefenseActive(), std::move(transaction),
                            std::move(changes), candidate.assets.nextAssetId(), tutorial});
  // Reserve only IDs, not ownership or a successful transaction. A concurrent
  // world corpse/drop can now allocate without stealing a provisional split ID.
  profile_.assets.setNextAssetIdForLoad(std::max(profile_.assets.nextAssetId(), candidate.assets.nextAssetId()));
  state.preview = std::move(candidate);
  state.previewBaseRevision = profile_.revision;
  state.previewBaseHighWater = profile_.assets.nextAssetId();
  state.lastAt = Clock::now();
  state.sinceLast = 0;
  if (wasEmpty) { state.firstAt = state.lastAt; state.sinceFirst = 0; }
  state.failed = false;
  return {id, true, true, receipt};
}

InventoryUiSubmission GameSession::requestBaseGroundContainerInventory(
    AssetInstanceId container, const BaseGroundAccess &access,
    const InventoryCommand &command, std::string transaction) {
  if (profileUiCommitPending())
    return {0, false, false, {false, false, DomainErrorCode::IllegalDestination, "PROFILE SAVE PENDING", profile_.revision}};
  if (alphaRaidActive_ || (!saveRepository_ && !baseDailyWriter_ && !baseDefenseWriter_)) {
    auto receipt = executeBaseGroundContainerInventory(container, access, command, std::move(transaction));
    return {0, receipt.succeeded, false, std::move(receipt)};
  }
  updateInventoryPersistence(0);
  const auto status = baseDefenseActive() ? baseDefenseCheckpointStatus() : baseDailyCheckpointStatus();
  if (status.failed || baseDailySaveBlocked_ || baseDefenseSaveBlocked_)
    return {0, false, false, {false, false, DomainErrorCode::InvalidProfile,
                            "Save protection is waiting for retry", profile_.revision}};
  if (!inventoryPersistence_) inventoryPersistence_ = std::make_shared<ProfileInventoryPersistenceState>();
  auto &state = *inventoryPersistence_;
  for (const auto &pending : state.requests)
    if (pending.transaction == transaction)
      return {pending.id, true, true, {true, true, DomainErrorCode::None, {}, state.preview.revision}};
  ProfileState candidate = inventoryCheckpointCandidate(profile_);
  AssetRegistry before = candidate.assets;
  const auto receipt = ::executeBaseGroundContainerInventory(candidate, publishedContentRegistry(),
      container, access, command, {candidate.revision, transaction});
  if (!receipt.succeeded || receipt.alreadyCommitted)
    return {0, receipt.succeeded, false, receipt};
  if (state.requests.size() >= 128 || state.nextRequestId == std::numeric_limits<std::uint64_t>::max())
    return {0, false, false, {false, false, DomainErrorCode::Capacity,
                            "Wait for the pending inventory saves", profile_.revision}};
  const auto id = state.nextRequestId++;
  const bool wasEmpty = state.requests.empty();
  const bool tutorial = advanceInventoryTutorial(candidate);
  state.requests.push_back({id, 0, baseDefenseActive(), std::move(transaction),
                            inventoryChanges(before, candidate.assets), candidate.assets.nextAssetId(), tutorial});
  profile_.assets.setNextAssetIdForLoad(std::max(profile_.assets.nextAssetId(), candidate.assets.nextAssetId()));
  state.preview = std::move(candidate);
  state.previewBaseRevision = profile_.revision;
  state.previewBaseHighWater = profile_.assets.nextAssetId();
  state.lastAt = Clock::now(); state.sinceLast = 0;
  if (wasEmpty) { state.firstAt = state.lastAt; state.sinceFirst = 0; }
  state.failed = false;
  return {id, true, true, receipt};
}

void GameSession::updateInventoryPersistence(float dt) {
  // Automatic Defense settlement shares the same writer. A paused/menu frame
  // still acknowledges its completed generation without running simulation.
  if (pendingBaseDefenseEnd_ && pendingBaseDefenseSettlement_ && baseDefenseWriter_) {
    const auto terminal = baseDefenseWriter_->snapshot();
    if (terminal.failed) {
      baseDefenseSaveBlocked_ = true;
      persistenceMessage_ = terminal.message;
    } else if (terminal.durableGeneration >= pendingBaseDefenseSettlementGeneration_)
      static_cast<void>(finalizeBaseDefense(*pendingBaseDefenseEnd_, false));
  }
  pollProfileUiPersistence();
  if (!inventoryPersistence_) return;
  auto &state = *inventoryPersistence_;
  if (profileUiCommitPending() && state.requests.empty()) {
    state.force = false;
    return;
  }
  const auto daily = baseDailyCheckpointStatus(), defense = baseDefenseCheckpointStatus();
  // Durable generations acknowledge the covered ordered prefix, including a
  // superseded pending generation whose successor contains all its effects.
  while (!state.requests.empty()) {
    const auto &request = state.requests.front();
    const auto &status = request.defense ? defense : daily;
    if (!request.firstGeneration || status.durableGeneration < request.firstGeneration) break;
    ProfileState candidate = profile_;
    if (!applyInventoryChanges(candidate, request)) {
      baseDailySaveBlocked_ = !baseDefenseActive();
      baseDefenseSaveBlocked_ = baseDefenseActive();
      persistenceMessage_ = "Saved inventory conflicts with a live transition; reload the saved checkpoint";
      state.failed = true;
      return;
    }
    profile_ = std::move(candidate);
    state.completions.push_back({request.id, true, {}});
    state.requests.pop_front();
  }
  const auto &status = baseDefenseActive() ? defense : daily;
  if (status.failed && !state.requests.empty()) {
    for (const auto &request : state.requests)
      state.completions.push_back({request.id, false, status.message});
    state.requests.clear();
    state.failed = true;
    state.force = false;
    // Supersede the failed UI suffix before an explicit retry can run it.
    // The writer remains paused; this does not secretly retry a failed disk.
    worldClockDirty_ = true;
    if (baseDefenseActive()) static_cast<void>(checkpointBaseDefense(false));
    else static_cast<void>(checkpointBaseDaily(false));
    return;
  }
  if (status.failed) return;
  state.failed = false;
  if (!state.requests.empty()) {
    if (state.previewBaseRevision != profile_.revision ||
        state.previewBaseHighWater != profile_.assets.nextAssetId()) {
      state.preview = inventoryCheckpointCandidate(profile_);
      state.previewBaseRevision = profile_.revision;
      state.previewBaseHighWater = profile_.assets.nextAssetId();
    }
    if (std::isfinite(dt) && dt > 0) {
      state.sinceFirst += dt; state.sinceLast += dt;
    }
    const bool unqueued = std::any_of(state.requests.begin(), state.requests.end(),
        [](const auto &request) { return !request.firstGeneration; });
    if (unqueued && (state.force || std::max(state.sinceLast, secondsSince(state.lastAt)) >= 0.15 ||
                    std::max(state.sinceFirst, secondsSince(state.firstAt)) >= 0.25)) {
      worldClockDirty_ = true;
      if (baseDefenseActive()) static_cast<void>(checkpointBaseDefense(false));
      else static_cast<void>(checkpointBaseDaily(false));
    }
  }
  if (state.force) {
    if (baseDefenseActive()) static_cast<void>(checkpointBaseDefense(false));
    else static_cast<void>(checkpointBaseDaily(false));
    state.force = false;
  }
}

void GameSession::requestInventoryPersistenceBarrier() {
  if (!inventoryPersistence_) inventoryPersistence_ = std::make_shared<ProfileInventoryPersistenceState>();
  inventoryPersistence_->force = true;
  updateInventoryPersistence(0);
}

bool GameSession::inventoryPersistenceBarrierReady() const noexcept {
  if (inventoryPersistencePending() || (inventoryPersistence_ && inventoryPersistence_->failed)) return false;
  const auto status = baseDefenseActive() ? baseDefenseCheckpointStatus() : baseDailyCheckpointStatus();
  return !status.failed && !status.outstanding() && !worldClockDirty_;
}
