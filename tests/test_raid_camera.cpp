#include <gtest/gtest.h>

#include <limits>

#include "raid_camera.h"

TEST(RaidCameraTest, SmallWorldNeverScrolls)
{
    const Vec2 offset = raidCameraOffset(
        {640.0F, 360.0F}, {960.0F, 640.0F}, {1280.0F, 720.0F});
    EXPECT_FLOAT_EQ(offset.x, 0.0F);
    EXPECT_FLOAT_EQ(offset.y, 0.0F);
}

TEST(RaidCameraTest, LargeWorldCentersAndClampsToEveryEdge)
{
    const Vec2 centered = raidCameraOffset(
        {1280.0F, 720.0F}, {2560.0F, 1440.0F}, {1280.0F, 720.0F});
    EXPECT_FLOAT_EQ(centered.x, 640.0F);
    EXPECT_FLOAT_EQ(centered.y, 360.0F);

    const Vec2 nearOrigin = raidCameraOffset(
        {100.0F, 100.0F}, {2560.0F, 1440.0F}, {1280.0F, 720.0F});
    EXPECT_FLOAT_EQ(nearOrigin.x, 0.0F);
    EXPECT_FLOAT_EQ(nearOrigin.y, 0.0F);

    const Vec2 farEdge = raidCameraOffset(
        {2500.0F, 1380.0F}, {2560.0F, 1440.0F}, {1280.0F, 720.0F});
    EXPECT_FLOAT_EQ(farEdge.x, 1280.0F);
    EXPECT_FLOAT_EQ(farEdge.y, 720.0F);
}

TEST(RaidCameraTest, ScreenAndWorldProjectionRoundTrip)
{
    const Vec2 camera{720.0F, 410.0F};
    const Vec2 world{1512.0F, 903.0F};
    const Vec2 screen = raidWorldToScreen(world, camera);
    const Vec2 repeated = raidScreenToWorld(screen, camera);
    EXPECT_FLOAT_EQ(repeated.x, world.x);
    EXPECT_FLOAT_EQ(repeated.y, world.y);
}

TEST(RaidCameraTest, NonFiniteInputFallsBackToStableOrigin)
{
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const Vec2 offset = raidCameraOffset(
        {nan, nan}, {2560.0F, 1440.0F}, {1280.0F, 720.0F});
    EXPECT_FLOAT_EQ(offset.x, 0.0F);
    EXPECT_FLOAT_EQ(offset.y, 0.0F);
}

TEST(RaidCameraTest, ReticleBoundsStayInsetInsideVisibleWorld)
{
    const Rect bounds = raidReticleWorldBounds(
        {640.0F, 360.0F},
        {2560.0F, 1440.0F},
        {1280.0F, 720.0F},
        48.0F);

    EXPECT_FLOAT_EQ(bounds.position.x, 688.0F);
    EXPECT_FLOAT_EQ(bounds.position.y, 408.0F);
    EXPECT_FLOAT_EQ(bounds.size.x, 1184.0F);
    EXPECT_FLOAT_EQ(bounds.size.y, 624.0F);
}

TEST(RaidCameraTest, ReticleBoundsCollapseSafelyForTinyWorld)
{
    const Rect bounds = raidReticleWorldBounds(
        {}, {96.0F, 64.0F}, {1280.0F, 720.0F}, 48.0F);

    EXPECT_FLOAT_EQ(bounds.position.x, 48.0F);
    EXPECT_FLOAT_EQ(bounds.position.y, 32.0F);
    EXPECT_FLOAT_EQ(bounds.size.x, 0.0F);
    EXPECT_FLOAT_EQ(bounds.size.y, 0.0F);
}
