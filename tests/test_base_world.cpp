#include <gtest/gtest.h>

#include <algorithm>

#include "base_world.h"

TEST(BaseWorldTest, MovementIsNormalizedAndClamped)
{
    BaseWorld world;
    const Vec2 before = world.playerPosition();
    BaseInput input;
    input.moveUp = true;
    input.moveRight = true;
    static_cast<void>(world.update(input, 1.0F));
    const Vec2 after = world.playerPosition();
    EXPECT_GT(after.x, before.x);
    EXPECT_LT(after.y, before.y);
    EXPECT_TRUE(world.playerIsMoving());
    EXPECT_GT(world.playerFacingDirection().x, 0.0F);
    EXPECT_LT(world.playerFacingDirection().y, 0.0F);

    input.sprint = true;
    for (int index = 0; index < 20; ++index)
    {
        static_cast<void>(world.update(input, 1.0F));
    }
    EXPECT_LE(world.playerPosition().x + world.playerSize().x, 1248.0F);
    EXPECT_GE(world.playerPosition().y, 24.0F);
}

TEST(BaseWorldTest, MovementAnimationResetsWhenPlayerStops)
{
    BaseWorld world;
    BaseInput input;
    input.moveLeft = true;
    static_cast<void>(world.update(input, 0.19F));
    EXPECT_TRUE(world.playerIsMoving());
    EXPECT_GT(world.playerAnimationFrame(), 0U);
    EXPECT_LT(world.playerFacingDirection().x, 0.0F);

    static_cast<void>(world.update(BaseInput{}, 0.01F));
    EXPECT_FALSE(world.playerIsMoving());
    EXPECT_EQ(world.playerAnimationFrame(), 0U);
}

TEST(BaseWorldTest, InteractionRequiresProximityAndExplicitInput)
{
    BaseWorld world;
    EXPECT_FALSE(world.update(BaseInput{}, 0.0F).has_value());

    world.resetAtRaidGate();
    EXPECT_EQ(
        world.interactableFacility(),
        BaseFacilityKind::RaidGate);
    BaseInput interact;
    interact.interactJustPressed = true;
    EXPECT_EQ(
        world.update(interact, 0.0F),
        BaseFacilityKind::RaidGate);
}

TEST(BaseWorldTest, ExposesDedicatedAllocationFacility)
{
    const BaseWorld world;
    ASSERT_EQ(world.facilities().size(), 4U);
    EXPECT_NE(
        std::find_if(
            world.facilities().begin(),
            world.facilities().end(),
            [](const BaseFacility &facility)
            { return facility.kind == BaseFacilityKind::Allocation; }),
        world.facilities().end());
    EXPECT_STREQ(
        baseFacilityName(BaseFacilityKind::Allocation),
        "ALLOCATION & NEEDS");
}
