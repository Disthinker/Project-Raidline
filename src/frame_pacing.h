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
};

[[nodiscard]] FramePacingConfiguration configureFramePacing(
    bool vsyncEnabled,
    float reportedRefreshHz) noexcept;

// Absolute-deadline software pacing. A missed deadline is abandoned instead
// of being replayed, so one long frame cannot create a burst of catch-up
// frames and another visible cadence discontinuity.
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
