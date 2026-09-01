#include <gtest/gtest.h>

#include <array>

#include "base_build_camera.h"

namespace
{
constexpr Vec2 kWorld{12800.0F, 7200.0F};
constexpr Vec2 kViewport{1280.0F, 720.0F};
}

TEST(BaseBuildCameraTest, ActivatesAtFocusAndDeactivatesToNoOffset)
{
    BaseBuildCameraController camera;
    camera.activate({6400.0F, 3600.0F}, kWorld, kViewport);
    EXPECT_TRUE(camera.active());
    EXPECT_FLOAT_EQ(camera.offset(kWorld, kViewport).x, 5760.0F);
    EXPECT_FLOAT_EQ(camera.offset(kWorld, kViewport).y, 3240.0F);

    camera.deactivate();
    EXPECT_FALSE(camera.active());
    EXPECT_FLOAT_EQ(camera.offset(kWorld, kViewport).x, 0.0F);
    EXPECT_FLOAT_EQ(camera.offset(kWorld, kViewport).y, 0.0F);
}

TEST(BaseBuildCameraTest, KeyboardPanUsesScreenConstantSpeedAndNormalizesDiagonal)
{
    BaseBuildCameraController camera;
    camera.activate({6400.0F, 3600.0F}, kWorld, kViewport);
    const Vec2 before = camera.offset(kWorld, kViewport);
    EXPECT_TRUE(camera.panKeyboard(
        {1.0F, 1.0F}, 1.0F, 600.0F, 1.0F, kWorld, kViewport));
    const Vec2 after = camera.offset(kWorld, kViewport);
    EXPECT_NEAR(after.x - before.x, 424.264F, 0.01F);
    EXPECT_NEAR(after.y - before.y, 424.264F, 0.01F);

    const Vec2 zoomedBefore = after;
    EXPECT_TRUE(camera.panKeyboard(
        {1.0F, 0.0F}, 1.0F, 600.0F, 0.5F, kWorld, {2560.0F, 1440.0F}));
    EXPECT_FLOAT_EQ(
        camera.offset(kWorld, {2560.0F, 1440.0F}).x -
            (zoomedBefore.x - 640.0F),
        1200.0F);
}

TEST(BaseBuildCameraTest, ShortRightPressRemainsClickWithoutMovingCamera)
{
    BaseBuildCameraController camera;
    camera.activate({6400.0F, 3600.0F}, kWorld, kViewport);
    const Vec2 before = camera.offset(kWorld, kViewport);
    camera.beginPointer({500.0F, 300.0F});
    EXPECT_FALSE(camera.updatePointer(
        {503.0F, 301.0F}, 1.0F, kWorld, kViewport));
    EXPECT_EQ(camera.endPointer(), BaseBuildPointerRelease::Click);
    EXPECT_EQ(camera.offset(kWorld, kViewport).x, before.x);
    EXPECT_EQ(camera.offset(kWorld, kViewport).y, before.y);
}

TEST(BaseBuildCameraTest, RightDragPansWorldAndDoesNotBecomeContextClick)
{
    BaseBuildCameraController camera;
    camera.activate({6400.0F, 3600.0F}, kWorld, kViewport);
    const Vec2 before = camera.offset(kWorld, kViewport);
    camera.beginPointer({500.0F, 300.0F});
    EXPECT_TRUE(camera.updatePointer(
        {600.0F, 350.0F}, 1.0F, kWorld, kViewport));
    const Vec2 after = camera.offset(kWorld, kViewport);
    EXPECT_FLOAT_EQ(after.x, before.x - 100.0F);
    EXPECT_FLOAT_EQ(after.y, before.y - 50.0F);
    EXPECT_EQ(camera.endPointer(), BaseBuildPointerRelease::Dragged);
}

TEST(BaseBuildCameraTest, KeyboardAndPointerPanClampToWorldEdges)
{
    BaseBuildCameraController camera;
    camera.activate({640.0F, 360.0F}, kWorld, kViewport);
    EXPECT_FALSE(camera.panKeyboard(
        {-1.0F, -1.0F}, 10.0F, 1000.0F, 1.0F, kWorld, kViewport));
    EXPECT_FLOAT_EQ(camera.offset(kWorld, kViewport).x, 0.0F);
    EXPECT_FLOAT_EQ(camera.offset(kWorld, kViewport).y, 0.0F);

    camera.beginPointer({200.0F, 200.0F});
    static_cast<void>(camera.updatePointer(
        {-50000.0F, -50000.0F}, 1.0F, kWorld, kViewport));
    const Vec2 farEdge = camera.offset(kWorld, kViewport);
    EXPECT_FLOAT_EQ(farEdge.x, 11520.0F);
    EXPECT_FLOAT_EQ(farEdge.y, 6480.0F);
}

TEST(BaseBuildCameraTest, WorldAndPointerProjectionRoundTripAtEveryBuildZoom)
{
    constexpr Vec2 camera{5723.5F, 3187.25F};
    constexpr Vec2 worldPoint{6184.0F, 3522.0F};
    constexpr Vec2 shake{3.0F, -2.0F};
    for (const float zoom :
         std::array{0.60F, 0.75F, 1.00F, 1.25F, 1.50F})
    {
        const Vec2 screen = baseBuildWorldToScreen(
            worldPoint, camera, zoom, shake);
        const Vec2 roundTrip = baseBuildScreenToWorld(
            screen, camera, zoom, shake);
        EXPECT_NEAR(roundTrip.x, worldPoint.x, 0.001F);
        EXPECT_NEAR(roundTrip.y, worldPoint.y, 0.001F);

        const Vec2 viewport = baseBuildViewportOrigin(
            camera, zoom, shake);
        EXPECT_NEAR(
            (viewport.x + worldPoint.x) * zoom,
            screen.x,
            0.001F);
        EXPECT_NEAR(
            (viewport.y + worldPoint.y) * zoom,
            screen.y,
            0.001F);
    }
}
