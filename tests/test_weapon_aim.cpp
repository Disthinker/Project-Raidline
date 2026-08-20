#include <gtest/gtest.h>

#include "weapon_aim.h"

TEST(WeaponAimStateTest, PointerTurnIsLimitedByHandlingSpeed)
{
    WeaponAimState aim{WeaponAimConfig{90.0F, 2.0F, 0.25F, 100.0F, 200.0F}};
    aim.update(Vec2{1.0F, 0.0F}, 50.0F, false, 0.0F);
    aim.update(Vec2{0.0F, 1.0F}, 50.0F, false, 0.5F);

    EXPECT_NEAR(aim.actualDirection().x, 0.7071F, 0.001F);
    EXPECT_NEAR(aim.actualDirection().y, 0.7071F, 0.001F);
}

TEST(WeaponAimStateTest, RecoilDoesNotRecoverWithoutPointerCorrection)
{
    WeaponAimState aim{WeaponAimConfig{360.0F, 4.0F, 0.25F, 100.0F, 200.0F}};
    aim.update(Vec2{1.0F, 0.0F}, 50.0F, false, 0.0F);
    aim.applyShotRecoil();
    const Vec2 kicked = aim.actualDirection();

    aim.update(Vec2{1.0F, 0.0F}, 50.0F, false, 5.0F);

    EXPECT_FLOAT_EQ(aim.recoilOffsetDegrees(), 4.0F);
    EXPECT_NEAR(aim.actualDirection().x, kicked.x, 0.0001F);
    EXPECT_NEAR(aim.actualDirection().y, kicked.y, 0.0001F);
}

TEST(WeaponAimStateTest, OppositePointerMovementCorrectsPersistentRecoil)
{
    WeaponAimState aim{WeaponAimConfig{720.0F, 4.0F, 0.25F, 100.0F, 200.0F}};
    aim.update(Vec2{1.0F, 0.0F}, 50.0F, false, 0.0F);
    aim.applyShotRecoil();
    aim.update(Vec2{0.997564F, -0.069756F}, 50.0F, false, 1.0F);

    EXPECT_NEAR(aim.actualDirection().x, 1.0F, 0.001F);
    EXPECT_NEAR(aim.actualDirection().y, 0.0F, 0.001F);
}

TEST(WeaponAimStateTest, AdsProgressAndRangeProjectionAreDeterministic)
{
    WeaponAimState aim{WeaponAimConfig{360.0F, 2.0F, 0.5F, 100.0F, 200.0F}};
    aim.update(Vec2{1.0F, 0.0F}, 150.0F, true, 0.25F);

    EXPECT_FLOAT_EQ(aim.aimDownSightsProgress(), 0.5F);
    EXPECT_FLOAT_EQ(aim.rangeSpreadFactor(), 0.5F);
    EXPECT_FLOAT_EQ(aim.damageMultiplier(), 1.0F);

    aim.update(Vec2{1.0F, 0.0F}, 201.0F, true, 0.25F);
    EXPECT_TRUE(aim.beyondMaximumRange());
    EXPECT_FLOAT_EQ(aim.damageMultiplier(), 0.25F);
}
