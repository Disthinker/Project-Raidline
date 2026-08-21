#include <gtest/gtest.h>

#include <limits>

#include "shot_resolution.h"

TEST(ShotResolutionTest, ValidCommandProducesNormalizedAcceptedResult)
{
    const ShotResolution result = resolveShotCommand(
        ShotCommand{
            7,
            Vec2{10.0F, 20.0F},
            Vec2{3.0F, 4.0F},
            1200.0F,
            8.0F,
            30,
            2048.0F,
            ShotAimIntent{91, HitRegion::Head, false}});

    ASSERT_TRUE(result.accepted());
    EXPECT_EQ(result.status, ShotResolutionStatus::Accepted);
    EXPECT_EQ(result.shotId, 7U);
    EXPECT_FLOAT_EQ(result.origin.x, 10.0F);
    EXPECT_FLOAT_EQ(result.origin.y, 20.0F);
    EXPECT_FLOAT_EQ(result.direction.x, 0.6F);
    EXPECT_FLOAT_EQ(result.direction.y, 0.8F);
    EXPECT_FLOAT_EQ(result.velocity.x, 720.0F);
    EXPECT_FLOAT_EQ(result.velocity.y, 960.0F);
    EXPECT_FLOAT_EQ(result.collisionExtent, 8.0F);
    EXPECT_EQ(result.damage, 30);
    EXPECT_FLOAT_EQ(result.maximumDistance, 2048.0F);
    EXPECT_FLOAT_EQ(result.impactPosition.x, 1238.8F);
    EXPECT_FLOAT_EQ(result.impactPosition.y, 1658.4F);
    ASSERT_TRUE(result.aimIntent.has_value());
    EXPECT_EQ(result.aimIntent->targetId, 91U);
    EXPECT_EQ(result.aimIntent->region, HitRegion::Head);
}

TEST(ShotResolutionTest, InvalidIdIsRejectedWithoutAcceptedPayload)
{
    const ShotResolution result = resolveShotCommand(
        ShotCommand{
            kInvalidShotId,
            Vec2{10.0F, 20.0F},
            Vec2{0.0F, -1.0F},
            1200.0F,
            8.0F,
            1});

    EXPECT_FALSE(result.accepted());
    EXPECT_EQ(
        result.status,
        ShotResolutionStatus::RejectedInvalidShotId);
}

TEST(ShotResolutionTest, NonFiniteOriginIsRejected)
{
    const ShotResolution result = resolveShotCommand(
        ShotCommand{
            1,
            Vec2{
                std::numeric_limits<float>::infinity(),
                20.0F},
            Vec2{0.0F, -1.0F},
            1200.0F,
            8.0F,
            1});

    EXPECT_EQ(
        result.status,
        ShotResolutionStatus::RejectedInvalidOrigin);
}

TEST(ShotResolutionTest, ZeroDirectionIsRejected)
{
    const ShotResolution result = resolveShotCommand(
        ShotCommand{
            1,
            Vec2{10.0F, 20.0F},
            Vec2{},
            1200.0F,
            8.0F,
            1});

    EXPECT_EQ(
        result.status,
        ShotResolutionStatus::RejectedInvalidDirection);
}

TEST(ShotResolutionTest, InvalidPhysicalInputsAreRejectedIndependently)
{
    const ShotCommand valid{
        1,
        Vec2{10.0F, 20.0F},
        Vec2{0.0F, -1.0F},
        1200.0F,
        8.0F,
        1};

    ShotCommand invalidSpeed = valid;
    invalidSpeed.speed = 0.0F;
    EXPECT_EQ(
        resolveShotCommand(invalidSpeed).status,
        ShotResolutionStatus::RejectedInvalidSpeed);

    ShotCommand invalidExtent = valid;
    invalidExtent.collisionExtent = -1.0F;
    EXPECT_EQ(
        resolveShotCommand(invalidExtent).status,
        ShotResolutionStatus::RejectedInvalidCollisionExtent);

    ShotCommand invalidDamage = valid;
    invalidDamage.damage = 0;
    EXPECT_EQ(
        resolveShotCommand(invalidDamage).status,
        ShotResolutionStatus::RejectedInvalidDamage);

    ShotCommand invalidDistance = valid;
    invalidDistance.maximumDistance = 0.0F;
    EXPECT_EQ(
        resolveShotCommand(invalidDistance).status,
        ShotResolutionStatus::RejectedInvalidMaximumDistance);

    ShotCommand invalidAimIntent = valid;
    invalidAimIntent.aimIntent = ShotAimIntent{};
    EXPECT_EQ(
        resolveShotCommand(invalidAimIntent).status,
        ShotResolutionStatus::RejectedInvalidAimIntent);

    invalidAimIntent.aimIntent = ShotAimIntent{
        7,
        static_cast<HitRegion>(255),
        false};
    EXPECT_EQ(
        resolveShotCommand(invalidAimIntent).status,
        ShotResolutionStatus::RejectedInvalidAimIntent);
}

TEST(ShotResolutionTest, StatusNamesCoverPublishedStates)
{
    EXPECT_STREQ(
        shotResolutionStatusName(
            ShotResolutionStatus::Accepted),
        "Accepted");
    EXPECT_STREQ(
        shotResolutionStatusName(
            ShotResolutionStatus::RejectedInvalidDamage),
        "RejectedInvalidDamage");
    EXPECT_STREQ(
        shotResolutionStatusName(
            ShotResolutionStatus::RejectedInvalidMaximumDistance),
        "RejectedInvalidMaximumDistance");
    EXPECT_STREQ(
        shotResolutionStatusName(
            ShotResolutionStatus::RejectedInvalidAimIntent),
        "RejectedInvalidAimIntent");
    EXPECT_STREQ(
        shotResolutionStatusName(
            static_cast<ShotResolutionStatus>(255)),
        "Unknown");
}
