#include "frame_pacing.h"

#include <cmath>
#include <limits>

namespace
{
constexpr float kFallbackRefreshHz{60.0F};

float usableRefreshRate(float reportedRefreshHz) noexcept
{
    // Reject stale/invalid display reports. The broad upper bound keeps high
    // refresh-rate panels valid without accepting values that would collapse
    // the software deadline into a busy loop.
    return std::isfinite(reportedRefreshHz) &&
                   reportedRefreshHz >= 24.0F &&
                   reportedRefreshHz <= 500.0F
        ? reportedRefreshHz
        : kFallbackRefreshHz;
}

std::uint64_t intervalFor(float refreshHz) noexcept
{
    constexpr double nanosecondsPerSecond{1'000'000'000.0};
    return static_cast<std::uint64_t>(
        std::llround(nanosecondsPerSecond / static_cast<double>(refreshHz)));
}

std::uint64_t saturatingAdd(
    std::uint64_t left,
    std::uint64_t right) noexcept
{
    return left > std::numeric_limits<std::uint64_t>::max() - right
        ? std::numeric_limits<std::uint64_t>::max()
        : left + right;
}
}

FramePacingConfiguration configureFramePacing(
    bool vsyncEnabled,
    float reportedRefreshHz) noexcept
{
    const float refreshHz = usableRefreshRate(reportedRefreshHz);
    return FramePacingConfiguration{
        vsyncEnabled
            ? FramePacingMode::VSync
            : FramePacingMode::SoftwareFallback,
        refreshHz,
        intervalFor(refreshHz)};
}

SoftwareFramePacer::SoftwareFramePacer(
    std::uint64_t targetIntervalNanoseconds) noexcept
{
    setTargetInterval(targetIntervalNanoseconds);
}

void SoftwareFramePacer::setTargetInterval(
    std::uint64_t targetIntervalNanoseconds) noexcept
{
    targetIntervalNanoseconds_ = targetIntervalNanoseconds == 0U
        ? 1U
        : targetIntervalNanoseconds;
    initialized_ = false;
    nextDeadlineNanoseconds_ = 0U;
}

void SoftwareFramePacer::reset(std::uint64_t nowNanoseconds) noexcept
{
    nextDeadlineNanoseconds_ = saturatingAdd(
        nowNanoseconds, targetIntervalNanoseconds_);
    initialized_ = true;
}

std::uint64_t SoftwareFramePacer::waitDuration(
    std::uint64_t nowNanoseconds) noexcept
{
    if (!initialized_)
    {
        reset(nowNanoseconds);
        return 0U;
    }

    if (nowNanoseconds >= nextDeadlineNanoseconds_)
    {
        // The current frame missed its presentation deadline. Start a fresh
        // cadence from now; never execute historical deadlines back-to-back.
        nextDeadlineNanoseconds_ = saturatingAdd(
            nowNanoseconds, targetIntervalNanoseconds_);
        return 0U;
    }

    const std::uint64_t wait = nextDeadlineNanoseconds_ - nowNanoseconds;
    nextDeadlineNanoseconds_ = saturatingAdd(
        nextDeadlineNanoseconds_, targetIntervalNanoseconds_);
    return wait;
}

std::uint64_t SoftwareFramePacer::nextDeadline() const noexcept
{
    return nextDeadlineNanoseconds_;
}
