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

TEST(RaidCameraTest, HospitalEntranceAndMovementStayVisibleInActiveSpace)
{
    const Vec2 world{2400.0F, 1440.0F};
    const Vec2 viewport{1280.0F, 720.0F};
    for (const Vec2 focus : {Vec2{1200.0F, 1380.0F}, Vec2{1850.0F, 810.0F},
                             Vec2{350.0F, 340.0F}, Vec2{1200.0F, 720.0F}})
    {
        const auto camera = raidCameraOffset(focus, world, viewport);
        const auto screen = raidWorldToScreen(focus, camera);
        EXPECT_GE(screen.x, 0.0F);
        EXPECT_LT(screen.x, viewport.x);
        EXPECT_GE(screen.y, 0.0F);
        EXPECT_LT(screen.y, viewport.y);
        EXPECT_FLOAT_EQ(raidScreenToWorld(screen, camera).x, focus.x);
    }
    EXPECT_NE(raidCameraOffset({1200, 720}, world, viewport).x,
              raidCameraOffset({1400, 720}, world, viewport).x);
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

TEST(RaidCameraTest, ReticleBoundsPermitOneDiameterOutsideVisibleWorld)
{
    const Rect bounds = raidReticleWorldBounds(
        {640.0F, 360.0F},
        {2560.0F, 1440.0F},
        {1280.0F, 720.0F},
        48.0F);

    EXPECT_FLOAT_EQ(bounds.position.x, 592.0F);
    EXPECT_FLOAT_EQ(bounds.position.y, 312.0F);
    EXPECT_FLOAT_EQ(bounds.size.x, 1376.0F);
    EXPECT_FLOAT_EQ(bounds.size.y, 816.0F);
}

TEST(RaidCameraTest, ReticleBoundsUseWholeTinyWorld)
{
    const Rect bounds = raidReticleWorldBounds(
        {}, {96.0F, 64.0F}, {1280.0F, 720.0F}, 48.0F);

    EXPECT_FLOAT_EQ(bounds.position.x, 0.0F);
    EXPECT_FLOAT_EQ(bounds.position.y, 0.0F);
    EXPECT_FLOAT_EQ(bounds.size.x, 96.0F);
    EXPECT_FLOAT_EQ(bounds.size.y, 64.0F);
}
