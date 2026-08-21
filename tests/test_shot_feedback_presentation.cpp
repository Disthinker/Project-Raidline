#include <cmath>
#include <limits>
#include <stdexcept>

#include <gtest/gtest.h>

#include "shot_feedback_presentation.h"

TEST(ShotFeedbackPresentationTest, RejectsInvalidConfiguration)
{
    ShotFeedbackPresentationConfig config;
    config.maximumSmokeOpacity = 0.50F;
    EXPECT_THROW(
        ShotFeedbackPresentationState{config},
        std::invalid_argument);
}

TEST(ShotFeedbackPresentationTest, AcceptedShotPublishesFlashSmokeAndTinyShake)
{
    ShotFeedbackPresentationState state;
    ASSERT_TRUE(state.recordAcceptedShot(
        7U,
        Vec2{320.0F, 240.0F},
        Vec2{3.0F, 4.0F}));

    const auto snapshots = state.snapshots();
    ASSERT_EQ(snapshots.size(), 1U);
    EXPECT_EQ(snapshots.front().shotId, 7U);
    EXPECT_FLOAT_EQ(snapshots.front().muzzleFlashIntensity, 1.0F);
    EXPECT_GT(snapshots.front().smokeOpacity, 0.0F);
    EXPECT_LE(snapshots.front().smokeOpacity, 0.18F);
    EXPECT_FLOAT_EQ(snapshots.front().smokeProgress, 0.0F);
    EXPECT_NEAR(snapshots.front().direction.x, 0.6F, 0.0001F);
    EXPECT_NEAR(snapshots.front().direction.y, 0.8F, 0.0001F);
    const Vec2 shake = state.normalizedScreenShakeOffset();
    EXPECT_GT(std::hypot(shake.x, shake.y), 0.50F);
    EXPECT_LE(std::hypot(shake.x, shake.y), 1.0F);
}

TEST(ShotFeedbackPresentationTest, FlashEndsBeforeQuickSmokeDissipation)
{
    ShotFeedbackPresentationState state;
    ASSERT_TRUE(state.recordAcceptedShot(
        1U,
        Vec2{100.0F, 100.0F},
        Vec2{1.0F, 0.0F}));

    state.update(0.060F);
    auto snapshots = state.snapshots();
    ASSERT_EQ(snapshots.size(), 1U);
    EXPECT_FLOAT_EQ(snapshots.front().muzzleFlashIntensity, 0.0F);
    EXPECT_GT(snapshots.front().smokeOpacity, 0.0F);
    EXPECT_GT(snapshots.front().smokeProgress, 0.0F);

    state.update(0.170F);
    EXPECT_TRUE(state.snapshots().empty());
    EXPECT_EQ(state.activeShotCount(), 0U);
}

TEST(ShotFeedbackPresentationTest, InvalidShotDoesNotChangeState)
{
    ShotFeedbackPresentationState state;
    EXPECT_FALSE(state.recordAcceptedShot(
        kInvalidShotId,
        Vec2{},
        Vec2{1.0F, 0.0F}));
    EXPECT_FALSE(state.recordAcceptedShot(
        1U,
        Vec2{std::numeric_limits<float>::quiet_NaN(), 0.0F},
        Vec2{1.0F, 0.0F}));
    EXPECT_FALSE(state.recordAcceptedShot(1U, Vec2{}, Vec2{}));
    EXPECT_EQ(state.activeShotCount(), 0U);
}

TEST(ShotFeedbackPresentationTest, AutomaticFireFeedbackRemainsBounded)
{
    ShotFeedbackPresentationState state;
    for (ShotId shotId{1U}; shotId <= 20U; ++shotId)
    {
        ASSERT_TRUE(state.recordAcceptedShot(
            shotId,
            Vec2{640.0F, 360.0F},
            Vec2{1.0F, 0.0F}));
    }

    EXPECT_EQ(state.activeShotCount(), 8U);
    const Vec2 shake = state.normalizedScreenShakeOffset();
    EXPECT_LE(std::hypot(shake.x, shake.y), 1.0F);
}
