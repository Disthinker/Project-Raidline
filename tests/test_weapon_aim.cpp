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

TEST(WeaponAimStateTest, NewTargetMovesFromCurrentReticlePosition)
{
    WeaponAimState aim{WeaponAimConfig{
        100.0F, 200.0F, 0.0F, 500.0F, 0.0F,
        0.25F, 100.0F, 500.0F}};
    aim.update(Vec2{500.0F, 100.0F}, kOrigin, kWorld, false, 0.0F);
    const Vec2 pointP = aim.actualWorldPosition();

    aim.update(Vec2{100.0F, 500.0F}, kOrigin, kWorld, false, 0.1F);
    const Vec2 moved = aim.actualWorldPosition();

    EXPECT_FLOAT_EQ(pointP.x, 500.0F);
    EXPECT_FLOAT_EQ(pointP.y, 100.0F);
    EXPECT_LT(moved.x, pointP.x);
    EXPECT_GT(moved.y, pointP.y);
    EXPECT_LT(magnitude(Vec2{moved.x - pointP.x, moved.y - pointP.y}), 11.0F);
}

TEST(WeaponAimStateTest, ControlAccelerationAndMaximumSpeedAreBounded)
{
    WeaponAimState aim{WeaponAimConfig{
        120.0F, 240.0F, 0.0F, 500.0F, 0.0F,
        0.25F, 100.0F, 500.0F}};
    aim.update(Vec2{200.0F, 100.0F}, kOrigin, kWorld, false, 0.0F);
    aim.update(Vec2{900.0F, 100.0F}, kOrigin, kWorld, false, 0.25F);

    EXPECT_LE(magnitude(aim.controlVelocity()), 60.01F);
    aim.update(Vec2{900.0F, 100.0F}, kOrigin, kWorld, false, 1.0F);
    EXPECT_LE(magnitude(aim.controlVelocity()), 120.01F);
}

TEST(WeaponAimStateTest, RecoilMovesOutwardAndDecelerates)
{
    WeaponAimState aim{WeaponAimConfig{
        400.0F, 800.0F, 300.0F, 600.0F, 0.0F,
        0.25F, 100.0F, 500.0F}};
    aim.update(Vec2{400.0F, 100.0F}, kOrigin, kWorld, false, 0.0F);
    aim.applyShotRecoil(kOrigin);

    EXPECT_NEAR(aim.recoilVelocity().x, 300.0F, 0.001F);
    EXPECT_NEAR(aim.recoilVelocity().y, 0.0F, 0.001F);
    aim.update(Vec2{400.0F, 100.0F}, kOrigin, kWorld, false, 0.25F);
    EXPECT_LT(magnitude(aim.recoilVelocity()), 300.0F);
    aim.update(Vec2{400.0F, 100.0F}, kOrigin, kWorld, false, 1.0F);
    EXPECT_FALSE(aim.recoilActive());
}

TEST(WeaponAimStateTest, NewShotRefreshesInsteadOfStackingRecoil)
{
    WeaponAimState aim{WeaponAimConfig{
        400.0F, 800.0F, 300.0F, 600.0F, 0.2F,
        0.25F, 100.0F, 500.0F, 42U}};
    aim.update(Vec2{400.0F, 100.0F}, kOrigin, kWorld, false, 0.0F);
    aim.applyShotRecoil(kOrigin);
    const float first = magnitude(aim.recoilVelocity());
    aim.applyShotRecoil(kOrigin);
    const float second = magnitude(aim.recoilVelocity());

    EXPECT_LE(first, 306.0F);
    EXPECT_LE(second, 306.0F);
}

TEST(WeaponAimStateTest, FramePartitionsRemainClose)
{
    const WeaponAimConfig config{
        500.0F, 1400.0F, 250.0F, 1600.0F, 0.1F,
        0.25F, 100.0F, 500.0F, 9U};
    WeaponAimState whole{config};
    WeaponAimState split{config};
    whole.update(Vec2{200.0F, 100.0F}, kOrigin, kWorld, false, 0.0F);
    split.update(Vec2{200.0F, 100.0F}, kOrigin, kWorld, false, 0.0F);
    whole.applyShotRecoil(kOrigin);
    split.applyShotRecoil(kOrigin);

    whole.update(Vec2{700.0F, 500.0F}, kOrigin, kWorld, false, 0.5F);
    for (int step = 0; step < 5; ++step)
    {
        split.update(
            Vec2{700.0F, 500.0F}, kOrigin, kWorld, false, 0.1F);
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
        500.0F, 1000.0F, 0.0F, 1000.0F, 0.0F,
        0.5F, 100.0F, 200.0F}};
    aim.update(Vec2{250.0F, 100.0F}, kOrigin, kWorld, true, 0.25F);

    EXPECT_FLOAT_EQ(aim.aimDownSightsProgress(), 0.5F);
    EXPECT_FLOAT_EQ(aim.rangeSpreadFactor(), 0.5F);
    EXPECT_FLOAT_EQ(aim.damageMultiplier(), 1.0F);

    aim.update(Vec2{301.0F, 100.0F}, kOrigin, kWorld, true, 1.0F);
    EXPECT_TRUE(aim.beyondMaximumRange());
    EXPECT_FLOAT_EQ(aim.damageMultiplier(), 0.25F);
}
