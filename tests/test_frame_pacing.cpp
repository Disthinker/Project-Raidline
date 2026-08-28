#include <gtest/gtest.h>

#include <cmath>
#include <limits>

#include "frame_pacing.h"

TEST(FramePacingTest, HighRefreshIsCappedAndDeadlineBacksBothModes)
{
    const FramePacingConfiguration vsync =
        configureFramePacing(true, 144.0F);
    EXPECT_EQ(vsync.mode, FramePacingMode::VSync);
    EXPECT_FLOAT_EQ(vsync.targetRefreshHz, 60.0F);
    EXPECT_TRUE(vsync.absoluteDeadlineEnabled);

    const FramePacingConfiguration fallback =
        configureFramePacing(false, 144.0F);
    EXPECT_EQ(fallback.mode, FramePacingMode::SoftwareFallback);
    EXPECT_FLOAT_EQ(fallback.targetRefreshHz, 60.0F);
    EXPECT_TRUE(fallback.absoluteDeadlineEnabled);
    EXPECT_NEAR(
        static_cast<double>(fallback.targetIntervalNanoseconds),
        1'000'000'000.0 / 60.0,
        1.0);
}

TEST(FramePacingTest, LowerRefreshDisplayKeepsItsNativeCadence)
{
    const FramePacingConfiguration configuration =
        configureFramePacing(true, 50.0F);
    EXPECT_FLOAT_EQ(configuration.targetRefreshHz, 50.0F);
    EXPECT_NEAR(
        static_cast<double>(configuration.targetIntervalNanoseconds),
        1'000'000'000.0 / 50.0,
        1.0);
}

TEST(FramePacingTest, InvalidDisplayRefreshUsesSixtyHertz)
{
    for (float invalid : {
             0.0F,
             -1.0F,
             std::numeric_limits<float>::quiet_NaN(),
             std::numeric_limits<float>::infinity(),
             1000.0F})
    {
        const FramePacingConfiguration configuration =
            configureFramePacing(false, invalid);
        EXPECT_FLOAT_EQ(configuration.targetRefreshHz, 60.0F);
        EXPECT_EQ(configuration.targetIntervalNanoseconds, 16'666'667U);
    }
}

TEST(FramePacingTest, AbsoluteDeadlineAdvancesWithoutAccumulatingDrift)
{
    SoftwareFramePacer pacer{10U};
    pacer.reset(100U);

    EXPECT_EQ(pacer.waitDuration(104U), 6U);
    EXPECT_EQ(pacer.nextDeadline(), 120U);
    EXPECT_EQ(pacer.waitDuration(117U), 3U);
    EXPECT_EQ(pacer.nextDeadline(), 130U);
}

TEST(FramePacingTest, MissedDeadlineResetsInsteadOfCatchingUp)
{
    SoftwareFramePacer pacer{10U};
    pacer.reset(100U);

    EXPECT_EQ(pacer.waitDuration(135U), 0U);
    EXPECT_EQ(pacer.nextDeadline(), 145U);
    EXPECT_EQ(pacer.waitDuration(140U), 5U);
    EXPECT_EQ(pacer.nextDeadline(), 155U);
}
