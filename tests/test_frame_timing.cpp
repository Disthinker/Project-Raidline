#include <gtest/gtest.h>

#include <limits>

#include "frame_timing.h"

TEST(FrameTimingTest, OrdinaryFrameDeltaIsPreserved)
{
    EXPECT_FLOAT_EQ(
        boundedFrameDeltaSeconds(1.0F / 60.0F),
        1.0F / 60.0F);
}

TEST(FrameTimingTest, LongFrameIsBounded)
{
    EXPECT_FLOAT_EQ(
        boundedFrameDeltaSeconds(2.0F),
        kMaximumFrameDeltaSeconds);
}

TEST(FrameTimingTest, InvalidFrameDeltaDoesNotAdvanceSimulation)
{
    EXPECT_FLOAT_EQ(boundedFrameDeltaSeconds(-1.0F), 0.0F);
    EXPECT_FLOAT_EQ(
        boundedFrameDeltaSeconds(
            std::numeric_limits<float>::infinity()),
        0.0F);
    EXPECT_FLOAT_EQ(
        boundedFrameDeltaSeconds(
            std::numeric_limits<float>::quiet_NaN()),
        0.0F);
}
