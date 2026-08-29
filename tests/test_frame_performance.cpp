#include <gtest/gtest.h>

#include <limits>

#include "frame_performance.h"

TEST(FramePerformanceTest, EmptyMonitorReturnsZeroSummary)
{
    const FramePerformanceSummary summary =
        FramePerformanceMonitor{}.summary();
    EXPECT_EQ(summary.sampleCount, 0U);
    EXPECT_FLOAT_EQ(summary.average.totalMilliseconds, 0.0F);
}

TEST(FramePerformanceTest, SummaryReportsAveragePercentileAndMaximum)
{
    FramePerformanceMonitor monitor;
    for (int value = 1; value <= 20; ++value)
    {
        const float milliseconds = static_cast<float>(value);
        monitor.record(FramePhaseDurations{
            milliseconds,
            milliseconds * 2.0F,
            milliseconds * 3.0F,
            milliseconds * 4.0F,
            milliseconds * 10.0F});
    }

    const FramePerformanceSummary summary = monitor.summary();
    EXPECT_EQ(summary.sampleCount, 20U);
    EXPECT_FLOAT_EQ(summary.average.eventMilliseconds, 10.5F);
    EXPECT_FLOAT_EQ(summary.percentile95.eventMilliseconds, 19.0F);
    EXPECT_FLOAT_EQ(summary.maximum.eventMilliseconds, 20.0F);
    EXPECT_FLOAT_EQ(summary.maximum.pacingMilliseconds, 80.0F);
    EXPECT_FLOAT_EQ(summary.maximum.totalMilliseconds, 200.0F);
}

TEST(FramePerformanceTest, RingBufferKeepsNewestSamples)
{
    FramePerformanceMonitor monitor;
    for (std::size_t index{};
         index < FramePerformanceMonitor::kCapacity + 10U;
         ++index)
    {
        monitor.record(FramePhaseDurations{
            0.0F, 0.0F, 0.0F, 0.0F, static_cast<float>(index)});
    }

    const FramePerformanceSummary summary = monitor.summary();
    EXPECT_EQ(summary.sampleCount, FramePerformanceMonitor::kCapacity);
    EXPECT_FLOAT_EQ(
        summary.maximum.totalMilliseconds,
        static_cast<float>(FramePerformanceMonitor::kCapacity + 9U));
}

TEST(FramePerformanceTest, InvalidDurationsAreSanitized)
{
    FramePerformanceMonitor monitor;
    monitor.record(FramePhaseDurations{
        -1.0F,
        std::numeric_limits<float>::infinity(),
        std::numeric_limits<float>::quiet_NaN(),
        -2.0F,
        -4.0F});

    const FramePerformanceSummary summary = monitor.summary();
    EXPECT_FLOAT_EQ(summary.maximum.eventMilliseconds, 0.0F);
    EXPECT_FLOAT_EQ(summary.maximum.updateMilliseconds, 0.0F);
    EXPECT_FLOAT_EQ(summary.maximum.renderMilliseconds, 0.0F);
    EXPECT_FLOAT_EQ(summary.maximum.pacingMilliseconds, 0.0F);
    EXPECT_FLOAT_EQ(summary.maximum.totalMilliseconds, 0.0F);
}
