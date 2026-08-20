#include <gtest/gtest.h>

#include "weapon_clear_gesture.h"

TEST(WeaponClearGestureTest, FourQualifiedReversalsWithinOneSecondComplete)
{
    WeaponClearGesture gesture;
    EXPECT_FALSE(gesture.observe({40.0F, 0.0F}, 0.00F));
    EXPECT_FALSE(gesture.observe({-40.0F, 0.0F}, 0.15F));
    EXPECT_FALSE(gesture.observe({40.0F, 0.0F}, 0.30F));
    EXPECT_FALSE(gesture.observe({-40.0F, 0.0F}, 0.45F));
    EXPECT_TRUE(gesture.observe({40.0F, 0.0F}, 0.60F));
    EXPECT_EQ(gesture.reversalCount(), 4U);
}

TEST(WeaponClearGestureTest, ShortSegmentsAndExpiredReversalsDoNotComplete)
{
    WeaponClearGesture gesture;
    EXPECT_FALSE(gesture.observe({20.0F, 0.0F}, 0.00F));
    EXPECT_FALSE(gesture.observe({-20.0F, 0.0F}, 0.10F));
    EXPECT_FALSE(gesture.observe({40.0F, 0.0F}, 0.20F));
    EXPECT_FALSE(gesture.observe({-40.0F, 0.0F}, 0.30F));
    EXPECT_FALSE(gesture.observe({40.0F, 0.0F}, 1.50F));
    EXPECT_LT(gesture.reversalCount(), 4U);
}

TEST(WeaponClearGestureTest, ResetDiscardsPartialProgress)
{
    WeaponClearGesture gesture;
    EXPECT_FALSE(gesture.observe({40.0F, 0.0F}, 0.0F));
    EXPECT_FALSE(gesture.observe({-40.0F, 0.0F}, 0.1F));
    gesture.reset();
    EXPECT_EQ(gesture.reversalCount(), 0U);
    EXPECT_FALSE(gesture.observe({40.0F, 0.0F}, 0.2F));
}
