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
    EXPECT_LT(world.playerFacingDirection().x, 0.0F);
}

TEST(BaseWorldTest, StoppingPreservesLastHorizontalFacing)
{
    BaseWorld world;
    BaseInput input;
    input.moveRight = true;
    static_cast<void>(world.update(input, 0.1F));
    ASSERT_GT(world.playerFacingDirection().x, 0.0F);

    input = BaseInput{};
    input.moveUp = true;
    static_cast<void>(world.update(input, 0.1F));
    ASSERT_GT(world.playerFacingDirection().x, 0.0F);

    static_cast<void>(world.update(BaseInput{}, 0.1F));

    EXPECT_FALSE(world.playerIsMoving());
    EXPECT_GT(world.playerFacingDirection().x, 0.0F);
}

TEST(BaseWorldTest, FacilityCollisionBlocksBothAxesAndAllowsSliding)
{
    BaseWorld horizontal;
    BaseInput moveLeft;
    moveLeft.moveLeft = true;
    for (int index{}; index < 30; ++index)
    {
        static_cast<void>(horizontal.update(moveLeft, 0.05F));
    }
    const Vec2 horizontalPosition = horizontal.playerPosition();
    EXPECT_GE(horizontalPosition.x, 304.0F);

    BaseInput diagonal;
    diagonal.moveLeft = true;
    diagonal.moveUp = true;
    const float beforeY = horizontal.playerPosition().y;
    for (int index{}; index < 10; ++index)
    {
        static_cast<void>(horizontal.update(diagonal, 0.05F));
    }
    EXPECT_GE(horizontal.playerPosition().x, 304.0F);
    EXPECT_LT(horizontal.playerPosition().y, beforeY);

    BaseWorld vertical;
    BaseInput moveUp;
    moveUp.moveUp = true;
    for (int index{}; index < 50; ++index)
    {
        static_cast<void>(vertical.update(moveUp, 0.05F));
    }
    EXPECT_GE(vertical.playerPosition().y, 132.0F);
}

TEST(BaseWorldTest, PureVerticalMovementCannotTunnelThroughFacilities)
{
    BaseWorld world;

    BaseInput moveUp;
    moveUp.moveUp = true;
    for (int index{}; index < 23; ++index)
    {
        static_cast<void>(world.update(moveUp, 0.05F));
    }

    BaseInput moveLeft;
    moveLeft.moveLeft = true;
    for (int index{}; index < 60; ++index)
    {
        static_cast<void>(world.update(moveLeft, 0.05F));
    }
    ASSERT_LT(world.playerPosition().x, 304.0F);
    ASSERT_LT(world.playerPosition().y + world.playerSize().y, 470.0F);

    BaseInput moveDownFast;
    moveDownFast.moveDown = true;
    moveDownFast.sprint = true;
    static_cast<void>(world.update(moveDownFast, 1.0F));
    EXPECT_FLOAT_EQ(world.playerPosition().y, 470.0F - world.playerSize().y);

    BaseInput moveUpFast;
    moveUpFast.moveUp = true;
    moveUpFast.sprint = true;
    static_cast<void>(world.update(moveUpFast, 2.0F));
    EXPECT_FLOAT_EQ(world.playerPosition().y, 364.0F);
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
    ASSERT_EQ(world.facilities().size(), 7U);
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

TEST(BaseWorldTest, ExposesDormitoryWithoutBlockingCentralRoute)
{
    BaseWorld world;
    EXPECT_NE(
        std::find_if(
            world.facilities().begin(),
            world.facilities().end(),
            [](const BaseFacility &facility)
            { return facility.kind == BaseFacilityKind::Dormitory; }),
        world.facilities().end());
    EXPECT_STREQ(
        baseFacilityName(BaseFacilityKind::Dormitory),
        "DORMITORY & REST");

    BaseInput moveLeft;
    moveLeft.moveLeft = true;
    for (int index{}; index < 20; ++index)
    {
        static_cast<void>(world.update(moveLeft, 0.05F));
    }
    EXPECT_EQ(world.interactableFacility(), BaseFacilityKind::Dormitory);
    BaseInput interact;
    interact.interactJustPressed = true;
    EXPECT_EQ(
        world.update(interact, 0.0F),
        BaseFacilityKind::Dormitory);

    world = BaseWorld{};
    BaseInput moveUp;
    moveUp.moveUp = true;
    for (int index{}; index < 50; ++index)
    {
        static_cast<void>(world.update(moveUp, 0.05F));
    }
    EXPECT_GE(world.playerPosition().y, 132.0F);
}

TEST(BaseWorldTest, ExposesAndInteractsWithMedicalFacility)
{
    BaseWorld world;
    EXPECT_NE(
        std::find_if(
            world.facilities().begin(),
            world.facilities().end(),
            [](const BaseFacility &facility)
            { return facility.kind == BaseFacilityKind::Medical; }),
        world.facilities().end());
    EXPECT_STREQ(
        baseFacilityName(BaseFacilityKind::Medical),
        "MEDICAL SERVICE");

    BaseInput moveRight;
    moveRight.moveRight = true;
    for (int index{}; index < 50; ++index)
    {
        static_cast<void>(world.update(moveRight, 0.05F));
    }
    EXPECT_EQ(world.interactableFacility(), BaseFacilityKind::Medical);
    BaseInput interact;
    interact.interactJustPressed = true;
    EXPECT_EQ(
        world.update(interact, 0.0F),
        BaseFacilityKind::Medical);
}

TEST(BaseWorldTest, ExposesWorkshopProductionFacility)
{
    const BaseWorld world;
    EXPECT_NE(
        std::find_if(
            world.facilities().begin(),
            world.facilities().end(),
            [](const BaseFacility &facility)
            { return facility.kind == BaseFacilityKind::Workshop; }),
        world.facilities().end());
    EXPECT_STREQ(
        baseFacilityName(BaseFacilityKind::Workshop),
        "WORKSHOP & PRODUCTION");
}
