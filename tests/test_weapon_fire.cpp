#include <gtest/gtest.h>

#include <cmath>
#include <limits>
#include <stdexcept>

#include "weapon_fire.h"

namespace
{
    constexpr float kTolerance{0.0001F};
}

TEST(WeaponFireStateTest, FirstShotIsExactAndStartsCooldownBloomAndRecoil)
{
    WeaponFireState fire;

    const auto shot = fire.update(
        true,
        Vec2{3.0F, 4.0F},
        0.0F);

    ASSERT_TRUE(shot.has_value());
    EXPECT_NEAR(shot->direction.x, 0.6F, kTolerance);
    EXPECT_NEAR(shot->direction.y, 0.8F, kTolerance);
    EXPECT_FLOAT_EQ(shot->spreadOffsetDegrees, 0.0F);
    EXPECT_FLOAT_EQ(fire.cooldownRemaining(), 0.12F);
    EXPECT_FLOAT_EQ(fire.spreadDegrees(), 1.0F);
    EXPECT_FLOAT_EQ(fire.visualRecoilPixels(), 3.0F);
}

TEST(WeaponFireStateTest, HeldTriggerRespectsCadenceAndBoundsFeedback)
{
    WeaponFireState fire;
    ASSERT_TRUE(fire.update(true, Vec2{0.0F, -1.0F}, 0.0F));
    EXPECT_FALSE(fire.update(true, Vec2{0.0F, -1.0F}, 0.11F));
    EXPECT_TRUE(fire.update(true, Vec2{0.0F, -1.0F}, 0.01F));

    for (int index = 0; index < 20; ++index)
    {
        const auto shot = fire.update(
            true,
            Vec2{0.0F, -1.0F},
            0.12F);
        ASSERT_TRUE(shot.has_value());
        EXPECT_LE(std::abs(shot->spreadOffsetDegrees), 6.0F);
    }

    EXPECT_FLOAT_EQ(fire.spreadDegrees(), 6.0F);
    EXPECT_FLOAT_EQ(fire.visualRecoilPixels(), 9.0F);
}

TEST(WeaponFireStateTest, EqualSeedsProduceTheSameSpreadSequence)
{
    WeaponFireState first;
    WeaponFireState second;

    for (int index = 0; index < 8; ++index)
    {
        const auto firstShot = first.update(
            true,
            Vec2{1.0F, 0.0F},
            index == 0 ? 0.0F : 0.12F);
        const auto secondShot = second.update(
            true,
            Vec2{1.0F, 0.0F},
            index == 0 ? 0.0F : 0.12F);

        ASSERT_TRUE(firstShot.has_value());
        ASSERT_TRUE(secondShot.has_value());
        EXPECT_FLOAT_EQ(
            firstShot->spreadOffsetDegrees,
            secondShot->spreadOffsetDegrees);
        EXPECT_FLOAT_EQ(firstShot->direction.x, secondShot->direction.x);
        EXPECT_FLOAT_EQ(firstShot->direction.y, secondShot->direction.y);
    }
}

TEST(WeaponFireStateTest, ReleasedTriggerRecoversAfterDelayAndResetsBurst)
{
    WeaponFireState fire;
    ASSERT_TRUE(fire.update(true, Vec2{1.0F, 0.0F}, 0.0F));

    EXPECT_FALSE(fire.update(false, Vec2{1.0F, 0.0F}, 0.05F));
    EXPECT_FLOAT_EQ(fire.spreadDegrees(), 1.0F);
    EXPECT_FLOAT_EQ(fire.visualRecoilPixels(), 3.0F);

    EXPECT_FALSE(fire.update(false, Vec2{1.0F, 0.0F}, 1.0F));
    EXPECT_FLOAT_EQ(fire.spreadDegrees(), 0.0F);
    EXPECT_FLOAT_EQ(fire.visualRecoilPixels(), 0.0F);

    const auto nextBurst = fire.update(
        true,
        Vec2{1.0F, 0.0F},
        0.0F);
    ASSERT_TRUE(nextBurst.has_value());
    EXPECT_FLOAT_EQ(nextBurst->spreadOffsetDegrees, 0.0F);
}

TEST(WeaponFireStateTest, InvalidAimOrDeltaTimeNeverCreatesInvalidState)
{
    WeaponFireState fire;
    EXPECT_FALSE(fire.update(true, Vec2{}, 0.0F));
    EXPECT_FALSE(fire.update(
        true,
        Vec2{1.0F, 0.0F},
        std::numeric_limits<float>::quiet_NaN()));

    EXPECT_FLOAT_EQ(fire.spreadDegrees(), 0.0F);
    EXPECT_TRUE(std::isfinite(fire.spreadDegrees()));

    EXPECT_TRUE(fire.update(true, Vec2{1.0F, 0.0F}, 0.0F));
}

TEST(WeaponFireStateTest, RejectsInconsistentConfiguration)
{
    WeaponFireConfig config;
    config.shotInterval = 0.0F;

    EXPECT_THROW(
        static_cast<void>(WeaponFireState{config}),
        std::invalid_argument);
}
