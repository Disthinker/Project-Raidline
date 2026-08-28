#pragma once

#include <cstdint>

enum class FramePacingMode
{
    VSync,
    SoftwareFallback
};

struct FramePacingConfiguration
{
    FramePacingMode mode{FramePacingMode::SoftwareFallback};
    float targetRefreshHz{60.0F};
    std::uint64_t targetIntervalNanoseconds{16'666'667U};
    bool absoluteDeadlineEnabled{true};
};

[[nodiscard]] FramePacingConfiguration configureFramePacing(
    bool vsyncEnabled,
    float reportedRefreshHz) noexcept;

// Absolute-deadline presentation pacing. It remains active even when a driver
// reports VSync: some remote/high-refresh presentation paths accept VSync but
// return from Present without a stable display cadence. If Present already
// blocked until the deadline this adds no second frame interval. A missed
// deadline is abandoned instead of being replayed.
class SoftwareFramePacer
{
public:
    explicit SoftwareFramePacer(
        std::uint64_t targetIntervalNanoseconds = 16'666'667U) noexcept;

    void setTargetInterval(
        std::uint64_t targetIntervalNanoseconds) noexcept;
    void reset(std::uint64_t nowNanoseconds) noexcept;

    [[nodiscard]] std::uint64_t waitDuration(
        std::uint64_t nowNanoseconds) noexcept;
    [[nodiscard]] std::uint64_t nextDeadline() const noexcept;

private:
    std::uint64_t targetIntervalNanoseconds_{16'666'667U};
    std::uint64_t nextDeadlineNanoseconds_{};
    bool initialized_{};
};
