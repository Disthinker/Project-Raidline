#include <gtest/gtest.h>

#include <cmath>
#include <limits>
#include <stdexcept>

#include "weapon_fire.h"

TEST(WeaponFireStateTest, FirstShotUsesMinimumAccuracyAndStartsCooldownBloom)
{
    WeaponFireConfig config;
    config.nearDistanceSpreadScale = 1.0F;
    WeaponFireState fire{config};
    WeaponFireContext close;
    close.distanceSpreadFactor = 0.0F;
    const auto shot = fire.update(
        true, Vec2{3.0F, 4.0F}, 0.0F, close);

    ASSERT_TRUE(shot.has_value());
    EXPECT_FLOAT_EQ(shot->spreadOffsetDegrees, 0.0F);
    EXPECT_FLOAT_EQ(fire.cooldownRemaining(), 0.12F);
    EXPECT_FLOAT_EQ(fire.spreadDegrees(), 1.0F);
}

TEST(WeaponFireStateTest, HeldTriggerRespectsCadenceAndMaximumSpread)
{
    WeaponFireState fire;
    ASSERT_TRUE(fire.update(true, Vec2{0.0F, -1.0F}, 0.0F));
    EXPECT_FALSE(fire.update(true, Vec2{0.0F, -1.0F}, 0.11F));
    EXPECT_TRUE(fire.update(true, Vec2{0.0F, -1.0F}, 0.01F));

    for (int index = 0; index < 20; ++index)
    {
        const auto shot = fire.update(true, Vec2{0.0F, -1.0F}, 0.12F);
        ASSERT_TRUE(shot.has_value());
        EXPECT_LE(std::abs(shot->spreadOffsetDegrees), 6.0F);
    }
    EXPECT_FLOAT_EQ(fire.spreadDegrees(), 6.0F);
}

TEST(WeaponFireStateTest, EqualSeedsProduceTheSameSpreadSequence)
{
    WeaponFireState first;
    WeaponFireState second;
    for (int index = 0; index < 8; ++index)
    {
        const float delta = index == 0 ? 0.0F : 0.12F;
        const auto firstShot = first.update(true, Vec2{1.0F, 0.0F}, delta);
        const auto secondShot = second.update(true, Vec2{1.0F, 0.0F}, delta);
        ASSERT_TRUE(firstShot.has_value());
        ASSERT_TRUE(secondShot.has_value());
        EXPECT_FLOAT_EQ(
            firstShot->spreadOffsetDegrees,
            secondShot->spreadOffsetDegrees);
        EXPECT_FLOAT_EQ(firstShot->direction.x, secondShot->direction.x);
        EXPECT_FLOAT_EQ(firstShot->direction.y, secondShot->direction.y);
    }
}

TEST(WeaponFireStateTest, ReleasedTriggerRecoversOnlyToContextualFloor)
{
    WeaponFireConfig config;
    config.nearDistanceSpreadScale = 1.0F;
    WeaponFireState fire{config};
    WeaponFireContext close;
    close.distanceSpreadFactor = 0.0F;
    ASSERT_TRUE(fire.update(
        true, Vec2{1.0F, 0.0F}, 0.0F, close));
    EXPECT_FALSE(fire.update(
        false, Vec2{1.0F, 0.0F}, 1.0F, close));
    EXPECT_FLOAT_EQ(fire.spreadDegrees(), 0.0F);

    WeaponFireContext moving;
    moving.moving = true;
    moving.distanceSpreadFactor = 0.0F;
    EXPECT_FALSE(fire.update(false, Vec2{1.0F, 0.0F}, 0.10F, moving));
    EXPECT_FLOAT_EQ(fire.spreadDegrees(), 2.1F);
}

TEST(WeaponFireStateTest, AdsImprovesAccuracyAndStability)
{
    WeaponFireState hip;
    WeaponFireState aimed;
    WeaponFireContext ads;
    ads.aimDownSightsProgress = 1.0F;

    ASSERT_TRUE(hip.update(true, Vec2{1.0F, 0.0F}, 0.0F));
    ASSERT_TRUE(aimed.update(true, Vec2{1.0F, 0.0F}, 0.0F, ads));
    const auto hipSecond = hip.update(true, Vec2{1.0F, 0.0F}, 0.12F);
    const auto aimedSecond = aimed.update(
        true, Vec2{1.0F, 0.0F}, 0.12F, ads);
    ASSERT_TRUE(hipSecond.has_value());
    ASSERT_TRUE(aimedSecond.has_value());
    for (int index = 0; index < 10; ++index)
    {
        ASSERT_TRUE(hip.update(
            true, Vec2{1.0F, 0.0F}, 0.12F));
        ASSERT_TRUE(aimed.update(
            true, Vec2{1.0F, 0.0F}, 0.12F, ads));
    }
    EXPECT_LT(aimed.spreadDegrees(), hip.spreadDegrees());
}

TEST(WeaponFireStateTest, DistanceEnvelopeAndReloadUseContextualMaximum)
{
    WeaponFireConfig config;
    config.minimumSpreadDegrees = 1.0F;
    WeaponFireState fire{config};
    WeaponFireContext close;
    close.distanceSpreadFactor = 0.0F;
    EXPECT_FALSE(fire.update(false, Vec2{1.0F, 0.0F}, 0.0F, close));
    EXPECT_FLOAT_EQ(fire.contextualMinimumSpreadDegrees(), 0.04F);
    EXPECT_FLOAT_EQ(fire.contextualMaximumSpreadDegrees(), 0.24F);
    EXPECT_FLOAT_EQ(fire.spreadDegrees(), 0.04F);

    WeaponFireContext beyond;
    beyond.distanceSpreadFactor = 1.0F;
    beyond.overEffectiveRangeFactor = 1.0F;
    EXPECT_FALSE(fire.update(false, Vec2{1.0F, 0.0F}, 0.0F, beyond));
    EXPECT_FLOAT_EQ(fire.contextualMinimumSpreadDegrees(), 1.5F);
    EXPECT_FLOAT_EQ(fire.contextualMaximumSpreadDegrees(), 9.0F);
    EXPECT_FLOAT_EQ(fire.spreadDegrees(), 2.1F);

    WeaponFireContext reload;
    reload.forceMaximumSpread = true;
    EXPECT_FALSE(fire.update(false, Vec2{1.0F, 0.0F}, 5.0F, reload));
    EXPECT_FLOAT_EQ(fire.spreadDegrees(), 6.0F);
}

TEST(WeaponFireStateTest, FastReticleMotionExpandsAndThenRecoversSpread)
{
    WeaponFireConfig config;
    config.nearDistanceSpreadScale = 1.0F;
    config.reticleMotionSpreadDegreesPerSecond = 10.0F;
    WeaponFireState fire{config};
    WeaponFireContext still;
    still.reticleControlSpeed = 100.0F;
    still.distanceSpreadFactor = 0.0F;
    EXPECT_FALSE(fire.update(false, Vec2{1.0F, 0.0F}, 0.10F, still));
    EXPECT_FLOAT_EQ(fire.spreadDegrees(), 0.0F);

    WeaponFireContext slight;
    slight.reticleControlSpeed = 180.0F;
    slight.distanceSpreadFactor = 0.0F;
    EXPECT_FALSE(fire.update(
        false, Vec2{1.0F, 0.0F}, 1.0F / 60.0F, slight));
    EXPECT_GT(fire.spreadDegrees(), 0.0F);
    EXPECT_LT(fire.spreadDegrees(), 0.10F);

    WeaponFireContext flick;
    flick.reticleControlSpeed = 1800.0F;
    flick.distanceSpreadFactor = 0.0F;
    EXPECT_FALSE(fire.update(
        false, Vec2{1.0F, 0.0F}, 1.0F / 60.0F, flick));
    EXPECT_GT(fire.spreadDegrees(), 0.10F);
    EXPECT_LT(fire.spreadDegrees(), 1.0F);

    for (int frame = 0; frame < 20; ++frame)
    {
        EXPECT_FALSE(fire.update(
            false, Vec2{1.0F, 0.0F}, 1.0F / 60.0F, flick));
    }
    EXPECT_GT(fire.spreadDegrees(), 3.0F);
    EXPECT_LT(fire.spreadDegrees(), 6.0F);

    // Recovery is continuous and deliberately slower than the old snap-back.
    const float expanded = fire.spreadDegrees();
    EXPECT_FALSE(fire.update(false, Vec2{1.0F, 0.0F}, 0.10F, still));
    EXPECT_LT(fire.spreadDegrees(), expanded);
    EXPECT_GT(fire.spreadDegrees(), expanded * 0.50F);
    EXPECT_FALSE(fire.update(false, Vec2{1.0F, 0.0F}, 2.0F, still));
    EXPECT_FLOAT_EQ(fire.spreadDegrees(), 0.0F);
}

TEST(WeaponFireStateTest,
     DistanceDefinesEnvelopeAndAddsModestRestingBloom)
{
    WeaponFireConfig config;
    config.minimumSpreadDegrees = 1.0F;
    config.maximumSpreadDegrees = 7.0F;
    WeaponFireState fire{config};

    WeaponFireContext close;
    close.distanceSpreadFactor = 0.0F;
    EXPECT_FALSE(fire.update(false, Vec2{1.0F, 0.0F}, 1.0F, close));
    EXPECT_FLOAT_EQ(fire.spreadDegrees(), 0.04F);

    WeaponFireContext middle;
    middle.distanceSpreadFactor = 0.5F;
    EXPECT_FALSE(fire.update(false, Vec2{1.0F, 0.0F}, 0.0F, middle));
    EXPECT_GT(fire.spreadDegrees(), fire.contextualMinimumSpreadDegrees());
    EXPECT_GT(fire.contextualMaximumSpreadDegrees(), 0.28F);
    EXPECT_LT(fire.contextualMaximumSpreadDegrees(), 7.0F);

    WeaponFireContext effective;
    effective.distanceSpreadFactor = 1.0F;
    EXPECT_FALSE(fire.update(false, Vec2{1.0F, 0.0F}, 0.0F, effective));
    EXPECT_FLOAT_EQ(fire.spreadPresentationFraction(), 0.08F);
    EXPECT_NEAR(fire.spreadDegrees(), 1.48F, 0.001F);
    EXPECT_FLOAT_EQ(fire.contextualMaximumSpreadDegrees(), 7.0F);
}

TEST(WeaponFireStateTest, MovingPlayerUsesReadablePortionOfSpreadEnvelope)
{
    WeaponFireConfig config;
    config.nearDistanceSpreadScale = 1.0F;
    config.movingSpreadFraction = 0.60F;
    WeaponFireState fire{config};
    WeaponFireContext moving;
    moving.moving = true;
    moving.distanceSpreadFactor = 0.0F;

    EXPECT_FALSE(fire.update(false, Vec2{1.0F, 0.0F}, 0.10F, moving));
    EXPECT_FLOAT_EQ(fire.spreadDegrees(), 3.6F);
    EXPECT_FLOAT_EQ(fire.spreadPresentationFraction(), 0.60F);
}

TEST(WeaponFireStateTest, SprintingImmediatelyOpensFartherThanWalking)
{
    WeaponFireConfig config;
    config.nearDistanceSpreadScale = 1.0F;
    config.movingSpreadFraction = 0.75F;
    config.sprintingSpreadFraction = 0.95F;

    WeaponFireContext walking;
    walking.moving = true;
    walking.distanceSpreadFactor = 0.0F;
    WeaponFireState walk{config};
    EXPECT_FALSE(walk.update(
        false, Vec2{1.0F, 0.0F}, 0.0F, walking));

    WeaponFireContext sprinting = walking;
    sprinting.sprinting = true;
    WeaponFireState sprint{config};
    EXPECT_FALSE(sprint.update(
        false, Vec2{1.0F, 0.0F}, 0.0F, sprinting));

    EXPECT_NEAR(walk.spreadPresentationFraction(), 0.60F, 0.001F);
    EXPECT_NEAR(sprint.spreadPresentationFraction(), 0.9025F, 0.001F);
    EXPECT_GT(sprint.spreadDegrees(), walk.spreadDegrees());
}

TEST(WeaponFireStateTest, PresentationFractionTracksAuthoritativeSpread)
{
    WeaponFireConfig config;
    config.nearDistanceSpreadScale = 1.0F;
    config.movingSpreadFraction = 0.75F;
    WeaponFireState fire{config};
    WeaponFireContext moving;
    moving.moving = true;
    moving.distanceSpreadFactor = 0.0F;

    EXPECT_FALSE(fire.update(false, Vec2{1.0F, 0.0F}, 0.10F, moving));
    EXPECT_FLOAT_EQ(fire.spreadDegrees(), 4.5F);
    EXPECT_FLOAT_EQ(fire.spreadPresentationFraction(), 0.75F);

    WeaponFireState restingAtEffectiveRange{config};
    WeaponFireContext effective;
    effective.distanceSpreadFactor = 1.0F;
    EXPECT_FALSE(restingAtEffectiveRange.update(
        false, Vec2{1.0F, 0.0F}, 0.0F, effective));
    EXPECT_FLOAT_EQ(
        restingAtEffectiveRange.spreadPresentationFraction(), 0.08F);
    EXPECT_GT(
        restingAtEffectiveRange.spreadDegrees(),
        restingAtEffectiveRange.contextualMinimumSpreadDegrees());
}

TEST(WeaponFireStateTest,
     MovementAndFlickRemainIndependentAtEffectiveRange)
{
    WeaponFireConfig config;
    config.minimumSpreadDegrees = 1.0F;
    config.maximumSpreadDegrees = 7.0F;
    config.nearDistanceSpreadScale = 1.0F;
    config.movingSpreadFraction = 0.75F;
    config.reticleMotionSpreadDegreesPerSecond = 20.0F;

    WeaponFireContext moving;
    moving.moving = true;
    moving.distanceSpreadFactor = 1.0F;
    WeaponFireState movementOnly{config};
    EXPECT_FALSE(movementOnly.update(
        false, Vec2{1.0F, 0.0F}, 0.10F, moving));

    WeaponFireContext flick;
    flick.reticleControlSpeed = 1800.0F;
    flick.distanceSpreadFactor = 1.0F;
    WeaponFireState motionOnly{config};
    EXPECT_FALSE(motionOnly.update(
        false, Vec2{1.0F, 0.0F}, 0.10F, flick));

    WeaponFireContext combined = moving;
    combined.reticleControlSpeed = 1800.0F;
    WeaponFireState both{config};
    EXPECT_FALSE(both.update(
        false, Vec2{1.0F, 0.0F}, 0.10F, combined));

    EXPECT_GT(movementOnly.spreadDegrees(), 1.0F);
    EXPECT_GT(motionOnly.spreadDegrees(), 1.0F);
    EXPECT_GT(both.spreadDegrees(), movementOnly.spreadDegrees());
    EXPECT_GT(both.spreadDegrees(), motionOnly.spreadDegrees());
    EXPECT_LE(both.spreadDegrees(), 7.0F);
}

TEST(WeaponFireStateTest, MotionBloomIsStableAcrossFramePartition)
{
    WeaponFireConfig config;
    config.nearDistanceSpreadScale = 1.0F;
    config.reticleMotionSpreadDegreesPerSecond = 20.0F;
    WeaponFireContext flick;
    flick.reticleControlSpeed = 1800.0F;
    flick.distanceSpreadFactor = 1.0F;

    WeaponFireState oneFrame{config};
    EXPECT_FALSE(oneFrame.update(
        false, Vec2{1.0F, 0.0F}, 0.10F, flick));

    WeaponFireState splitFrames{config};
    for (int index = 0; index < 10; ++index)
    {
        EXPECT_FALSE(splitFrames.update(
            false, Vec2{1.0F, 0.0F}, 0.01F, flick));
    }

    EXPECT_NEAR(
        splitFrames.spreadDegrees(),
        oneFrame.spreadDegrees(),
        0.001F);
}

TEST(WeaponFireStateTest, InvalidAimOrDeltaTimeNeverCreatesInvalidState)
{
    WeaponFireState fire;
    EXPECT_FALSE(fire.update(true, Vec2{}, 0.0F));
    EXPECT_FALSE(fire.update(
        true,
        Vec2{1.0F, 0.0F},
        std::numeric_limits<float>::quiet_NaN()));
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
