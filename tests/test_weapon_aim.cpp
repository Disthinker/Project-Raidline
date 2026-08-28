#include <gtest/gtest.h>

#include <cmath>

#include "weapon_aim.h"

namespace
{
    constexpr Vec2 kOrigin{100.0F, 100.0F};
    constexpr Vec2 kWorld{1000.0F, 800.0F};

    float magnitude(Vec2 value)
    {
        return std::sqrt(value.x * value.x + value.y * value.y);
    }
}

TEST(WeaponAimStateTest, HighMagnificationMovesFromCurrentReticlePosition)
{
    WeaponAimState aim{WeaponAimConfig{
        100.0F, 200.0F, 0.0F, 500.0F, 0.0F, 0.05F,
        0.25F, 100.0F, 500.0F}};
    aim.update(Vec2{500.0F, 100.0F}, kOrigin, kWorld, false, 0.0F);
    const Vec2 pointP = aim.actualWorldPosition();

    aim.update(
        Vec2{100.0F, 500.0F}, kOrigin, kWorld, false, 0.1F,
        std::nullopt, AimControlMode::HighMagnificationInertial);
    const Vec2 moved = aim.actualWorldPosition();

    EXPECT_FLOAT_EQ(pointP.x, 500.0F);
    EXPECT_FLOAT_EQ(pointP.y, 100.0F);
    EXPECT_LT(moved.x, pointP.x);
    EXPECT_GT(moved.y, pointP.y);
    EXPECT_LT(magnitude(Vec2{moved.x - pointP.x, moved.y - pointP.y}), 11.0F);
}

TEST(WeaponAimStateTest, HighMagnificationSpeedAndAccelerationAreBounded)
{
    WeaponAimState aim{WeaponAimConfig{
        120.0F, 240.0F, 0.0F, 500.0F, 0.0F, 0.05F,
        0.25F, 100.0F, 500.0F}};
    aim.update(Vec2{200.0F, 100.0F}, kOrigin, kWorld, false, 0.0F);
    aim.update(
        Vec2{900.0F, 100.0F}, kOrigin, kWorld, false, 0.25F,
        std::nullopt, AimControlMode::HighMagnificationInertial);

    EXPECT_LE(magnitude(aim.controlVelocity()), 60.01F);
    aim.update(
        Vec2{900.0F, 100.0F}, kOrigin, kWorld, false, 1.0F,
        std::nullopt, AimControlMode::HighMagnificationInertial);
    EXPECT_LE(magnitude(aim.controlVelocity()), 120.01F);
}

TEST(WeaponAimStateTest, RecoilMovesOutwardAndDecelerates)
{
    WeaponAimState aim{WeaponAimConfig{
        400.0F, 800.0F, 300.0F, 600.0F, 0.0F, 0.05F,
        0.25F, 100.0F, 500.0F}};
    aim.update(Vec2{400.0F, 100.0F}, kOrigin, kWorld, false, 0.0F);
    aim.applyShotRecoil(kOrigin);

    EXPECT_NEAR(aim.recoilVelocity().x, 300.0F, 0.001F);
    EXPECT_NEAR(aim.recoilVelocity().y, 0.0F, 0.001F);
    aim.update(Vec2{400.0F, 100.0F}, kOrigin, kWorld, false, 0.05F);
    EXPECT_GT(aim.recoilVelocity().x, 250.0F);
    aim.update(Vec2{400.0F, 100.0F}, kOrigin, kWorld, false, 0.25F);
    EXPECT_LT(magnitude(aim.recoilVelocity()), 300.0F);
    aim.update(Vec2{400.0F, 100.0F}, kOrigin, kWorld, false, 1.0F);
    EXPECT_FALSE(aim.recoilActive());
}

TEST(WeaponAimStateTest, StationaryMouseDoesNotAutomaticallyRecoverRecoil)
{
    WeaponAimState aim{WeaponAimConfig{
        800.0F, 1600.0F, 300.0F, 600.0F, 0.0F, 0.05F,
        0.25F, 100.0F, 500.0F}};
    constexpr Vec2 originalAim{400.0F, 100.0F};
    aim.update(originalAim, kOrigin, kWorld, false, 0.0F);
    aim.applyShotRecoil(kOrigin);
    aim.update(originalAim, kOrigin, kWorld, false, 1.0F);

    ASSERT_FALSE(aim.recoilActive());
    const Vec2 displaced = aim.actualWorldPosition();
    EXPECT_GT(displaced.x, originalAim.x + 1.0F);
    EXPECT_NEAR(aim.targetWorldPosition().x, displaced.x, 0.01F);

    aim.update(originalAim, kOrigin, kWorld, false, 1.0F);
    EXPECT_NEAR(aim.actualWorldPosition().x, displaced.x, 0.01F);
    EXPECT_NEAR(aim.actualWorldPosition().y, displaced.y, 0.01F);
}

TEST(WeaponAimStateTest, OpposingMouseMotionRecoversDisplacedReticle)
{
    WeaponAimState aim{WeaponAimConfig{
        800.0F, 1600.0F, 300.0F, 600.0F, 0.0F, 0.05F,
        0.25F, 100.0F, 500.0F}};
    constexpr Vec2 originalAim{400.0F, 100.0F};
    aim.update(originalAim, kOrigin, kWorld, false, 0.0F);
    aim.applyShotRecoil(kOrigin);
    aim.update(originalAim, kOrigin, kWorld, false, 1.0F);
    const float displacedX = aim.actualWorldPosition().x;

    aim.update(Vec2{350.0F, 100.0F}, kOrigin, kWorld, false, 0.5F);
    EXPECT_LT(aim.actualWorldPosition().x, displacedX);
}

TEST(WeaponAimStateTest, RelativeMotionMovesAimWithoutAbsoluteCursorTravel)
{
    WeaponAimState aim{WeaponAimConfig{
        3000.0F, 9000.0F, 0.0F, 600.0F, 0.0F, 0.05F,
        0.25F, 100.0F, 500.0F}};
    constexpr Vec2 fixedOsCursor{999.0F, 400.0F};
    aim.update(fixedOsCursor, kOrigin, kWorld, false, 0.0F);

    aim.update(
        fixedOsCursor, kOrigin, kWorld, false, 0.1F,
        Vec2{-120.0F, 0.0F});

    EXPECT_FLOAT_EQ(aim.actualWorldPosition().x, 879.0F);
    EXPECT_FLOAT_EQ(aim.targetWorldPosition().x, 879.0F);
}

TEST(WeaponAimStateTest, RelativeAimFollowsScrollingViewportWorldAnchor)
{
    WeaponAimState aim{WeaponAimConfig{
        3000.0F, 9000.0F, 0.0F, 600.0F, 0.0F, 0.05F,
        0.25F, 100.0F, 500.0F}};
    constexpr Vec2 initialOrigin{400.0F, 300.0F};
    constexpr Vec2 initialWorldAnchor{900.0F, 360.0F};
    aim.update(initialWorldAnchor, initialOrigin, kWorld, false, 0.0F);
    const Vec2 initialDirection = aim.actualDirection();

    constexpr Vec2 viewportWorldDelta{72.0F, 36.0F};
    const Vec2 movedOrigin{
        initialOrigin.x + viewportWorldDelta.x,
        initialOrigin.y + viewportWorldDelta.y};
    const Vec2 movedWorldAnchor{
        initialWorldAnchor.x + viewportWorldDelta.x,
        initialWorldAnchor.y + viewportWorldDelta.y};
    aim.update(
        movedWorldAnchor,
        movedOrigin,
        kWorld,
        false,
        0.1F,
        Vec2{});

    EXPECT_FLOAT_EQ(aim.actualWorldPosition().x, movedWorldAnchor.x);
    EXPECT_FLOAT_EQ(aim.actualWorldPosition().y, movedWorldAnchor.y);
    EXPECT_FLOAT_EQ(aim.targetWorldPosition().x, movedWorldAnchor.x);
    EXPECT_FLOAT_EQ(aim.targetWorldPosition().y, movedWorldAnchor.y);
    EXPECT_NEAR(aim.actualDirection().x, initialDirection.x, 0.0001F);
    EXPECT_NEAR(aim.actualDirection().y, initialDirection.y, 0.0001F);
}

TEST(WeaponAimStateTest, HipAndLowPowerAdsRespondDirectlyToPointerMotion)
{
    WeaponAimState hip;
    WeaponAimState ads;
    hip.update(Vec2{400.0F, 100.0F}, kOrigin, kWorld, false, 0.0F);
    ads.update(Vec2{400.0F, 100.0F}, kOrigin, kWorld, true, 0.0F);

    constexpr Vec2 motion{-75.0F, 45.0F};
    hip.update(
        Vec2{400.0F, 100.0F}, kOrigin, kWorld, false, 0.05F, motion);
    ads.update(
        Vec2{400.0F, 100.0F}, kOrigin, kWorld, true, 0.05F, motion);

    EXPECT_FLOAT_EQ(hip.actualWorldPosition().x, 325.0F);
    EXPECT_FLOAT_EQ(hip.actualWorldPosition().y, 145.0F);
    EXPECT_FLOAT_EQ(ads.actualWorldPosition().x, 325.0F);
    EXPECT_FLOAT_EQ(ads.actualWorldPosition().y, 145.0F);
    EXPECT_NEAR(hip.reticleControlSpeed(), magnitude(motion) / 0.05F, 0.01F);
}

TEST(WeaponAimStateTest, HighLateralRecoilStartsOutwardThenBendsSmoothly)
{
    WeaponAimState aim{WeaponAimConfig{
        3000.0F, 9000.0F, 600.0F, 1200.0F, 1.0F, 0.05F,
        0.25F, 100.0F, 500.0F, 42U}};
    aim.update(Vec2{500.0F, 100.0F}, kOrigin, kWorld, false, 0.0F);
    aim.applyShotRecoil(kOrigin);

    EXPECT_NEAR(magnitude(aim.recoilVelocity()), 600.0F, 0.01F);
    EXPECT_NEAR(aim.recoilVelocity().y, 0.0F, 0.01F);
    const Vec2 before = aim.actualWorldPosition();
    aim.update(Vec2{500.0F, 100.0F}, kOrigin, kWorld, false, 0.01F);
    const Vec2 earlyVelocity = aim.recoilVelocity();
    EXPECT_GT(std::abs(earlyVelocity.y), 0.0F);
    EXPECT_LT(magnitude(aim.recoilVelocity()), 600.0F);
    EXPECT_LT(
        magnitude(Vec2{
            aim.actualWorldPosition().x - before.x,
            aim.actualWorldPosition().y - before.y}),
        7.0F);
    aim.update(Vec2{500.0F, 100.0F}, kOrigin, kWorld, false, 0.07F);
    EXPECT_GT(std::abs(aim.recoilVelocity().y), std::abs(earlyVelocity.y));
    EXPECT_NEAR(magnitude(aim.recoilVelocity()), 504.0F, 2.0F);
}

TEST(WeaponAimStateTest, NewShotRefreshesInsteadOfStackingRecoil)
{
    WeaponAimState aim{WeaponAimConfig{
        400.0F, 800.0F, 300.0F, 600.0F, 0.2F, 0.05F,
        0.25F, 100.0F, 500.0F, 42U}};
    aim.update(Vec2{400.0F, 100.0F}, kOrigin, kWorld, false, 0.0F);
    aim.applyShotRecoil(kOrigin);
    aim.update(Vec2{400.0F, 100.0F}, kOrigin, kWorld, false, 0.05F);
    const float first = magnitude(aim.recoilVelocity());
    aim.applyShotRecoil(kOrigin);
    aim.update(Vec2{400.0F, 100.0F}, kOrigin, kWorld, false, 0.05F);
    const float second = magnitude(aim.recoilVelocity());

    EXPECT_LE(first, 306.0F);
    EXPECT_LE(second, 306.0F);
}

TEST(WeaponAimStateTest, FramePartitionsRemainClose)
{
    const WeaponAimConfig config{
        500.0F, 1400.0F, 250.0F, 1600.0F, 0.1F, 0.05F,
        0.25F, 100.0F, 500.0F, 9U};
    WeaponAimState whole{config};
    WeaponAimState split{config};
    whole.update(Vec2{200.0F, 100.0F}, kOrigin, kWorld, false, 0.0F);
    split.update(Vec2{200.0F, 100.0F}, kOrigin, kWorld, false, 0.0F);
    whole.applyShotRecoil(kOrigin);
    split.applyShotRecoil(kOrigin);

    whole.update(
        Vec2{700.0F, 500.0F}, kOrigin, kWorld, false, 0.5F,
        std::nullopt, AimControlMode::HighMagnificationInertial);
    for (int step = 0; step < 5; ++step)
    {
        split.update(
            Vec2{700.0F, 500.0F}, kOrigin, kWorld, false, 0.1F,
            std::nullopt, AimControlMode::HighMagnificationInertial);
    }

    EXPECT_NEAR(
        whole.actualWorldPosition().x,
        split.actualWorldPosition().x,
        1.0F);
    EXPECT_NEAR(
        whole.actualWorldPosition().y,
        split.actualWorldPosition().y,
        1.0F);
}

TEST(WeaponAimStateTest, AdsAndActualRangeProjectionAreDeterministic)
{
    WeaponAimState aim{WeaponAimConfig{
        500.0F, 1000.0F, 0.0F, 1000.0F, 0.0F, 0.05F,
        0.5F, 100.0F, 200.0F}};
    aim.update(Vec2{250.0F, 100.0F}, kOrigin, kWorld, true, 0.25F);

    EXPECT_FLOAT_EQ(aim.aimDownSightsProgress(), 0.5F);
    EXPECT_FLOAT_EQ(aim.distanceSpreadFactor(), 1.0F);
    EXPECT_FLOAT_EQ(aim.overEffectiveRangeFactor(), 0.5F);
    EXPECT_FLOAT_EQ(aim.damageMultiplier(), 0.625F);
    EXPECT_TRUE(aim.beyondEffectiveRange());

    aim.update(Vec2{301.0F, 100.0F}, kOrigin, kWorld, true, 1.0F);
    EXPECT_TRUE(aim.beyondMaximumRange());
    EXPECT_FLOAT_EQ(aim.damageMultiplier(), 0.25F);
}
