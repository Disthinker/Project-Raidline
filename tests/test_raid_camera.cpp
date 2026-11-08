#include <gtest/gtest.h>

#include <limits>

#include "item_definition.h"
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

TEST(RaidCameraTest, ReticleBoundsStayInsideLastWindowPixel)
{
    const Rect bounds = raidReticleWorldBounds(
        {640.0F, 360.0F},
        {1280.0F, 720.0F});

    EXPECT_FLOAT_EQ(bounds.position.x, 640.0F);
    EXPECT_FLOAT_EQ(bounds.position.y, 360.0F);
    EXPECT_FLOAT_EQ(bounds.size.x, 1279.0F);
    EXPECT_FLOAT_EQ(bounds.size.y, 719.0F);
}

TEST(RaidCameraTest, SmallWorldReticleStillUsesFullWindow)
{
    const auto camera = raidCameraOffset({48,32}, {96,64}, {1280,720});
    const Rect bounds = raidReticleWorldBounds(
        camera, {1280.0F, 720.0F});

    EXPECT_FLOAT_EQ(bounds.position.x, 0.0F);
    EXPECT_FLOAT_EQ(bounds.position.y, 0.0F);
    EXPECT_FLOAT_EQ(bounds.size.x, 1279.0F);
    EXPECT_FLOAT_EQ(bounds.size.y, 719.0F);
}

TEST(RaidCameraTest, InvalidViewportIsRejectedAndSinglePixelIsBounded)
{
    const float nan = std::numeric_limits<float>::quiet_NaN();
    EXPECT_FALSE(validRaidViewport({{nan,0}, {1280,720}}));
    EXPECT_FALSE(validRaidViewport({{}, {0,720}}));
    EXPECT_FALSE(validRaidViewport({{}, {1280,nan}}));
    const auto bounds = raidReticleWorldBounds({123,456},{1,1});
    EXPECT_FLOAT_EQ(bounds.position.x,123);
    EXPECT_FLOAT_EQ(bounds.position.y,456);
    EXPECT_FLOAT_EQ(bounds.size.x,0);
    EXPECT_FLOAT_EQ(bounds.size.y,0);
}

TEST(RaidCameraTest, OpticPanAndZoomRevealSpacePastTheOrdinaryWindow)
{
    const Vec2 world{4000.0F, 2200.0F}, player{1600.0F, 900.0F};
    const Vec2 screen{1280.0F, 720.0F}, right{1279.0F, 360.0F};
    const auto ordinary = weaponOpticProjection(WeaponOpticMode::None, player, world, screen, right);
    const auto low = weaponOpticProjection(WeaponOpticMode::Low, player, world, screen, right);
    const auto high = weaponOpticProjection(WeaponOpticMode::High, player, world, screen, right);
    EXPECT_FLOAT_EQ(low.worldBounds().size.x, 1600.0F);
    EXPECT_FLOAT_EQ(low.lensRadius, 0.0F);
    EXPECT_LT(low.worldToScreen(player).x, screen.x * 0.5F);
    EXPECT_FLOAT_EQ(high.scale, 0.90F);
    EXPECT_FLOAT_EQ(high.lensRadius, screen.y * 0.27F);
    EXPECT_LT(high.worldToScreen(player).x, low.worldToScreen(player).x);
    const auto ordinaryEdge = ordinary.screenToWorld(right);
    EXPECT_GT(low.screenToWorld(right).x, ordinaryEdge.x + 300.0F);
    EXPECT_GT(high.screenToWorld(right).x, ordinaryEdge.x + 380.0F);
    for (const auto &view : {low, high})
    {
        const auto bounds = view.reticleBounds();
        const auto edge = view.worldToScreen({bounds.position.x + bounds.size.x,
                                              bounds.position.y + bounds.size.y});
        EXPECT_NEAR(edge.x, screen.x - 1.0F, 0.001F);
        EXPECT_NEAR(edge.y, screen.y - 1.0F, 0.001F);
        const auto recovered = view.worldToScreen(view.screenToWorld({1187.5F, 415.0F}));
        EXPECT_NEAR(recovered.x, 1187.5F, 0.001F);
        EXPECT_NEAR(recovered.y, 415.0F, 0.001F);
    }
    const auto smallRoom = weaponOpticProjection(WeaponOpticMode::High, {100, 100},
        {450, 300}, screen, right);
    EXPECT_FLOAT_EQ(smallRoom.camera.x, 0.0F);
    EXPECT_FLOAT_EQ(smallRoom.camera.y, 0.0F);
}


TEST(RaidCameraTest, OpticTransitionClampsAndPreservesItsSettledProjection)
{
    const Vec2 world{4000.0F, 2200.0F}, player{1800.0F, 900.0F};
    const Vec2 screen{1280.0F, 720.0F}, intent{1190.0F, 360.0F};
    const auto ordinary = weaponOpticProjection(WeaponOpticMode::None, player, world, screen, intent);
    for (const auto mode : {WeaponOpticMode::Low, WeaponOpticMode::High})
    {
        const auto closed = weaponOpticProjection(mode, player, world, screen, intent, 0.0F);
        const auto halfway = weaponOpticProjection(mode, player, world, screen, intent, 0.5F);
        const auto settled = weaponOpticProjection(mode, player, world, screen, intent);
        EXPECT_FLOAT_EQ(closed.transitionWeight, 0.0F);
        EXPECT_FLOAT_EQ(closed.scale, ordinary.scale);
        EXPECT_FLOAT_EQ(closed.camera.x, ordinary.camera.x);
        EXPECT_FLOAT_EQ(closed.camera.y, ordinary.camera.y);
        EXPECT_FLOAT_EQ(halfway.transitionWeight, 0.5F);
        EXPECT_FLOAT_EQ(halfway.scale, (1.0F + settled.scale) / 2.0F);
        EXPECT_FLOAT_EQ(halfway.lensRadius, settled.lensRadius);
        EXPECT_LT(settled.worldToScreen(player).x, halfway.worldToScreen(player).x);
        EXPECT_LT(halfway.worldToScreen(player).x, ordinary.worldToScreen(player).x);
        EXPECT_FLOAT_EQ(settled.transitionWeight, 1.0F);
        const auto smallRoom = weaponOpticProjection(mode, {100, 100}, {450, 300}, screen, intent, 0.5F);
        EXPECT_FLOAT_EQ(smallRoom.camera.x, 0.0F);
        EXPECT_FLOAT_EQ(smallRoom.camera.y, 0.0F);
    }
}
