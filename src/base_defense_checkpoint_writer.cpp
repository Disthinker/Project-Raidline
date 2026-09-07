#include "base_defense_checkpoint_writer.h"

#include <limits>
#include <stdexcept>
#include <utility>

namespace {
double milliseconds(std::chrono::steady_clock::duration duration) {
  return std::chrono::duration<double, std::milli>{duration}.count();
}
} // namespace

BaseDefenseCheckpointWriter::BaseDefenseCheckpointWriter(
    SaveRepository repository)
    : BaseDefenseCheckpointWriter{
          [repository = std::move(repository)](const ProfileState &profile,
                                               std::string_view version,
                                               SaveWriteMetrics *metrics) {
            return repository.save(profile, version, metrics);
          }} {}

BaseDefenseCheckpointWriter::BaseDefenseCheckpointWriter(WriteFunction write)
    : write_{std::move(write)} {
  if (!write_) {
    throw std::invalid_argument{"checkpoint writer requires a save operation"};
  }
  worker_ = std::thread{[this] { run(); }};
}

BaseDefenseCheckpointWriter::~BaseDefenseCheckpointWriter() {
  {
    const std::lock_guard lock{mutex_};
    stopping_ = true;
  }
  wake_.notify_all();
  // Drain already accepted work once. A failed write terminates the worker;
  // destruction never repeatedly retries a broken or unavailable store.
  worker_.join();
}

std::uint64_t
BaseDefenseCheckpointWriter::request(const ProfileState &profile,
                                     std::string_view contentVersion) {
  const auto capturedAt = Clock::now();
  auto candidate = std::move(retiredCopyBuffer_);
  if (candidate) {
    *candidate = profile;
  } else {
    candidate = std::make_unique<ProfileState>(profile);
  }
  std::string version{contentVersion};
  const double copyMilliseconds = milliseconds(Clock::now() - capturedAt);
  std::uint64_t generation{};
  std::optional<Checkpoint> retired;
  {
    const std::lock_guard lock{mutex_};
    if (stopping_) {
      throw std::logic_error{"checkpoint writer is stopping"};
    }
    if (status_.requestedGeneration ==
        std::numeric_limits<std::uint64_t>::max()) {
      throw std::overflow_error{"checkpoint generation exhausted"};
    }
    if (!profileId_.empty() && profileId_ != profile.profileId) {
      throw std::invalid_argument{
          "checkpoint writer cannot change Profile identity"};
    }
    profileId_ = profile.profileId;
    if (!status_.outstanding()) {
      lagStartedAt_ = capturedAt;
    }
    generation = ++status_.requestedGeneration;
    if (pending_) {
      ++status_.coalescedRequests;
      // Never hold the queue lock while destroying an obsolete large
      // Registry: the worker must be able to acknowledge durable writes.
      retired = std::move(pending_);
      pending_.reset();
    }
    pending_ = Checkpoint{generation, capturedAt, std::move(candidate),
                          std::move(version)};
    status_.pending = true;
    status_.lastCopyMilliseconds = copyMilliseconds;
  }
  wake_.notify_one();
  if (retired) {
    // This buffer was superseded only after its complete replacement was
    // accepted. No worker can reference it; keep one bounded spare instead
    // of releasing/reallocating the whole Registry on every coalescence.
    retiredCopyBuffer_ = std::move(retired->profile);
  }
  return generation;
}

BaseDefenseCheckpointStatus BaseDefenseCheckpointWriter::snapshot() const {
  const std::lock_guard lock{mutex_};
  BaseDefenseCheckpointStatus result = status_;
  if (result.outstanding()) {
    result.durabilityLagMilliseconds =
        milliseconds(Clock::now() - lagStartedAt_);
  }
  return result;
}

SaveWriteResult BaseDefenseCheckpointWriter::flush() {
  std::unique_lock lock{mutex_};
  const std::uint64_t target = status_.requestedGeneration;
  completed_.wait(lock, [&] {
    return status_.durableGeneration >= target || status_.failed;
  });
  if (status_.durableGeneration >= target) {
    return {true, status_.message};
  }
  return {false, status_.message};
}

bool BaseDefenseCheckpointWriter::retry() {
  {
    const std::lock_guard lock{mutex_};
    if (stopping_ || !status_.failed || !pending_) {
      return false;
    }
    status_.failed = false;
  }
  wake_.notify_one();
  return true;
}

void BaseDefenseCheckpointWriter::run() {
  for (;;) {
    Checkpoint current;
    {
      std::unique_lock lock{mutex_};
      wake_.wait(lock, [&] {
        return stopping_ || (pending_.has_value() && !status_.failed);
      });
      if (stopping_ && (!pending_ || status_.failed)) {
        return;
      }
      current = std::move(*pending_);
      pending_.reset();
      status_.pending = false;
      status_.inFlightGeneration = current.generation;
    }

    SaveWriteResult result;
    SaveWriteMetrics metrics;
    const auto startedAt = Clock::now();
    try {
      result = write_(std::as_const(*current.profile), current.contentVersion,
                      &metrics);
    } catch (const std::exception &error) {
      result = {false, error.what()};
    } catch (...) {
      result = {false, "checkpoint store failed with an unknown exception"};
    }
    metrics.totalMilliseconds = milliseconds(Clock::now() - startedAt);

    {
      const std::lock_guard lock{mutex_};
      status_.lastWriteMetrics = metrics;
      status_.inFlightGeneration = 0;
      status_.message = std::move(result.message);
      if (result.succeeded) {
        status_.durableGeneration = current.generation;
        lagStartedAt_ = current.capturedAt;
      } else {
        status_.failed = true;
        if (!pending_) {
          pending_ = std::move(current);
          status_.pending = true;
        }
        if (status_.message.empty()) {
          status_.message = "Base defense checkpoint could not be saved";
        }
      }
    }
    completed_.notify_all();
  }
}
