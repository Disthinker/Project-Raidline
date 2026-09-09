#include "game_session.h"

#include <algorithm>
#include <cmath>

namespace {
constexpr float kDailyCheckpointSeconds = 0.25F;
constexpr double kMaximumDailySaveLagMilliseconds = 1000.0;
} // namespace

bool GameSession::baseDailySaveBlocked() const noexcept {
  return baseDailySaveBlocked_;
}

BaseDefenseCheckpointStatus GameSession::baseDailyCheckpointStatus() const {
  return baseDailyWriter_ ? baseDailyWriter_->snapshot()
                          : BaseDefenseCheckpointStatus{};
}

bool GameSession::prepareBaseDailyFrame(float deltaTime) {
  const auto status = baseDailyCheckpointStatus();
  baseDailySaveBlocked_ =
      status.failed ||
      (status.outstanding() &&
       status.durabilityLagMilliseconds >= kMaximumDailySaveLagMilliseconds);
  if (baseDailySaveBlocked_) {
    persistenceMessage_ = "BASE SAVE PROTECTION PAUSED | RETRY";
    return false;
  }
  if (std::isfinite(deltaTime) && deltaTime > 0.0F)
    baseDailyCheckpointElapsed_ =
        std::min(kDailyCheckpointSeconds,
                 baseDailyCheckpointElapsed_ + std::min(deltaTime, 0.1F));
  return true;
}

void GameSession::finishBaseDailyFrame() {
  if (worldClockDirty_ &&
      baseDailyCheckpointElapsed_ >= kDailyCheckpointSeconds)
    static_cast<void>(checkpointBaseDaily(false));
}

bool GameSession::checkpointBaseDaily(bool wait) {
  if (baseDefenseActive() || alphaRaidActive_ || profile_.pendingRaid)
    return false;
  if (!worldClockDirty_ && !baseDailyWriter_)
    return true;
  if (wait && !saveRepository_) {
    const auto validation =
        validateProfileState(profile_, publishedContentRegistry());
    if (!validation.valid) {
      persistenceMessage_ = validation.message;
      return false;
    }
  }
  if (worldClockDirty_) {
    if (saveRepository_) {
      if (!baseDailyWriter_)
        baseDailyWriter_ =
            std::make_unique<BaseDefenseCheckpointWriter>(*saveRepository_);
      // One coherent copy contains ammunition, injuries, death tombstones
      // and surviving positions. JSON/validation/atomic replacement run
      // only on the worker; it never observes the live Profile or World.
      static_cast<void>(baseDailyWriter_->request(
          profile_, publishedContentRegistry().contentVersion()));
    }
    worldClockDirty_ = false; // queued, NOT a claim of durable completion
    worldClockCheckpointElapsedSeconds_ = 0.0F;
    baseDailyCheckpointElapsed_ = 0.0F;
  }
  if (wait && baseDailyWriter_) {
    const auto saved = baseDailyWriter_->flush();
    baseDailySaveBlocked_ = !saved.succeeded;
    persistenceMessage_ =
        saved.succeeded ? saved.message : "BASE SAVE PROTECTION PAUSED | RETRY";
    return saved.succeeded;
  }
  return true;
}

bool GameSession::retryBaseDailySave() {
  if (baseDefenseActive() || alphaRaidActive_ || profile_.pendingRaid)
    return false;
  if (!saveRepository_)
    return checkpointBaseDaily(true);
  // Queue the latest accepted state BEFORE retrying an obsolete failed copy.
  if (!checkpointBaseDaily(false))
    return false;
  if (baseDailyWriter_ && baseDailyWriter_->snapshot().failed)
    static_cast<void>(baseDailyWriter_->retry());
  return checkpointBaseDaily(true);
}

bool GameSession::drainBaseDailyCheckpoint() {
  if (!baseDailyWriter_ &&
      (!worldClockDirty_ || baseDefenseActive() || alphaRaidActive_))
    return true;
  if (!checkpointBaseDaily(true))
    return false;
  // A synchronous command/save or a Defense writer may now take ownership.
  // No obsolete background generation can overwrite that newer transaction.
  baseDailyWriter_.reset();
  baseDailyCheckpointElapsed_ = 0.0F;
  baseDailySaveBlocked_ = false;
  return true;
}
