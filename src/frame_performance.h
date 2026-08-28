#pragma once

#include <array>
#include <cstddef>

struct FramePhaseDurations
{
    float eventMilliseconds{};
    float updateMilliseconds{};
    float renderMilliseconds{};
    float pacingMilliseconds{};
    float totalMilliseconds{};
};

struct FramePerformanceSummary
{
    std::size_t sampleCount{};
    FramePhaseDurations average{};
    FramePhaseDurations percentile95{};
    FramePhaseDurations maximum{};
};

// SDL client wall-time telemetry. Simulation never reads this monitor: it only
// helps developers correlate deterministic workload counters with frame cost.
class FramePerformanceMonitor
{
public:
    static constexpr std::size_t kCapacity{120U};

    void record(FramePhaseDurations sample) noexcept;
    [[nodiscard]] FramePerformanceSummary summary() const;
    [[nodiscard]] std::size_t sampleCount() const noexcept;

private:
    std::array<FramePhaseDurations, kCapacity> samples_{};
    std::size_t nextSample_{};
    std::size_t sampleCount_{};
};
