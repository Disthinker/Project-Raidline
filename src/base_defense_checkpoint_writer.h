#pragma once

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

#include "save_repository.h"

struct BaseDefenseCheckpointStatus {
  std::uint64_t requestedGeneration{};
  std::uint64_t durableGeneration{};
  std::uint64_t inFlightGeneration{};
  std::uint64_t coalescedRequests{};
  bool pending{};
  bool failed{};
  std::string message;
  double durabilityLagMilliseconds{};
  double lastCopyMilliseconds{};
  SaveWriteMetrics lastWriteMetrics;

  [[nodiscard]] bool outstanding() const noexcept {
    return durableGeneration < requestedGeneration;
  }
};

// One Profile owner submits coherent snapshots on the main thread. The worker
// owns only immutable copies. While this writer exists, every write to its
// repository MUST pass through it (including terminal/quit barriers).
class BaseDefenseCheckpointWriter {
public:
  using WriteFunction = std::function<SaveWriteResult(
      const ProfileState &, std::string_view, SaveWriteMetrics *)>;

  explicit BaseDefenseCheckpointWriter(SaveRepository repository);
  // A narrow filesystem seam for deterministic slow/failing-store tests.
  explicit BaseDefenseCheckpointWriter(WriteFunction write);
  ~BaseDefenseCheckpointWriter();

  BaseDefenseCheckpointWriter(const BaseDefenseCheckpointWriter &) = delete;
  BaseDefenseCheckpointWriter &
  operator=(const BaseDefenseCheckpointWriter &) = delete;

  // Copies exactly once, outside the queue lock. Supersedes only the pending
  // snapshot; an in-flight atomic write is never cancelled or overwritten.
  // A retired, unqueued buffer may be reused by the submitting thread only.
  [[nodiscard]] std::uint64_t request(const ProfileState &profile,
                                      std::string_view contentVersion);
  [[nodiscard]] BaseDefenseCheckpointStatus snapshot() const;

  // Start/end/quit callers must await this BEFORE acknowledging persistence.
  // A failed write returns immediately; it never retries in an unbounded loop.
  [[nodiscard]] SaveWriteResult flush();
  // Retry the newest coherent snapshot, not an obsolete failed generation.
  [[nodiscard]] bool retry();

private:
  using Clock = std::chrono::steady_clock;
  struct Checkpoint {
    std::uint64_t generation{};
    Clock::time_point capturedAt;
    // Exclusive ownership freezes this buffer until it leaves the queue
    // or its write finishes. The worker passes only a const reference to
    // WriteFunction; it never mutates the Profile or its Registry.
    std::unique_ptr<ProfileState> profile;
    std::string contentVersion;
  };

  void run();

  WriteFunction write_;
  mutable std::mutex mutex_;
  std::condition_variable wake_;
  std::condition_variable completed_;
  std::optional<Checkpoint> pending_;
  // Main-thread-only scratch, never a third work request. At most three
  // buffers exist: in-flight, pending, and this retired/copying buffer.
  // A failed copy discards scratch without touching the accepted pending.
  std::unique_ptr<ProfileState> retiredCopyBuffer_;
  BaseDefenseCheckpointStatus status_;
  std::string profileId_;
  Clock::time_point lagStartedAt_{};
  bool stopping_{};
  std::thread worker_;
};
