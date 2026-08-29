#include "frame_performance.h"

#include <algorithm>
#include <cmath>
#include <utility>
#include <vector>

namespace
{
    float sanitize(float value) noexcept
    {
        return std::isfinite(value) && value >= 0.0F ? value : 0.0F;
    }

    FramePhaseDurations sanitized(FramePhaseDurations sample) noexcept
    {
        sample.eventMilliseconds = sanitize(sample.eventMilliseconds);
        sample.updateMilliseconds = sanitize(sample.updateMilliseconds);
        sample.renderMilliseconds = sanitize(sample.renderMilliseconds);
        sample.pacingMilliseconds = sanitize(sample.pacingMilliseconds);
        sample.totalMilliseconds = sanitize(sample.totalMilliseconds);
        return sample;
    }

    void accumulate(
        FramePhaseDurations &destination,
        const FramePhaseDurations &sample) noexcept
    {
        destination.eventMilliseconds += sample.eventMilliseconds;
        destination.updateMilliseconds += sample.updateMilliseconds;
        destination.renderMilliseconds += sample.renderMilliseconds;
        destination.pacingMilliseconds += sample.pacingMilliseconds;
        destination.totalMilliseconds += sample.totalMilliseconds;
    }

    FramePhaseDurations divided(
        FramePhaseDurations value,
        float divisor) noexcept
    {
        value.eventMilliseconds /= divisor;
        value.updateMilliseconds /= divisor;
        value.renderMilliseconds /= divisor;
        value.pacingMilliseconds /= divisor;
        value.totalMilliseconds /= divisor;
        return value;
    }

    FramePhaseDurations maximumOf(
        FramePhaseDurations current,
        const FramePhaseDurations &sample) noexcept
    {
        current.eventMilliseconds = std::max(
            current.eventMilliseconds, sample.eventMilliseconds);
        current.updateMilliseconds = std::max(
            current.updateMilliseconds, sample.updateMilliseconds);
        current.renderMilliseconds = std::max(
            current.renderMilliseconds, sample.renderMilliseconds);
        current.pacingMilliseconds = std::max(
            current.pacingMilliseconds, sample.pacingMilliseconds);
        current.totalMilliseconds = std::max(
            current.totalMilliseconds, sample.totalMilliseconds);
        return current;
    }
}

void FramePerformanceMonitor::record(FramePhaseDurations sample) noexcept
{
    samples_[nextSample_] = sanitized(sample);
    nextSample_ = (nextSample_ + 1U) % kCapacity;
    sampleCount_ = std::min(sampleCount_ + 1U, kCapacity);
}

FramePerformanceSummary FramePerformanceMonitor::summary() const
{
    FramePerformanceSummary result;
    result.sampleCount = sampleCount_;
    if (sampleCount_ == 0U)
    {
        return result;
    }

    std::vector<float> events;
    std::vector<float> updates;
    std::vector<float> renders;
    std::vector<float> pacing;
    std::vector<float> totals;
    events.reserve(sampleCount_);
    updates.reserve(sampleCount_);
    renders.reserve(sampleCount_);
    pacing.reserve(sampleCount_);
    totals.reserve(sampleCount_);
    for (std::size_t index{}; index < sampleCount_; ++index)
    {
        const FramePhaseDurations &sample = samples_[index];
        accumulate(result.average, sample);
        result.maximum = maximumOf(result.maximum, sample);
        events.push_back(sample.eventMilliseconds);
        updates.push_back(sample.updateMilliseconds);
        renders.push_back(sample.renderMilliseconds);
        pacing.push_back(sample.pacingMilliseconds);
        totals.push_back(sample.totalMilliseconds);
    }
    result.average = divided(
        result.average, static_cast<float>(sampleCount_));

    const std::size_t percentileIndex =
        (sampleCount_ * 95U + 99U) / 100U - 1U;
    const auto percentile = [percentileIndex](std::vector<float> values)
    {
        std::sort(values.begin(), values.end());
        return values[percentileIndex];
    };
    result.percentile95 = FramePhaseDurations{
        percentile(std::move(events)),
        percentile(std::move(updates)),
        percentile(std::move(renders)),
        percentile(std::move(pacing)),
        percentile(std::move(totals))};
    return result;
}

std::size_t FramePerformanceMonitor::sampleCount() const noexcept
{
    return sampleCount_;
}
