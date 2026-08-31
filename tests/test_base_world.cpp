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
    EXPECT_LE(world.playerPosition().x + world.playerSize().x,
              world.worldSize().x - 24.0F);
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
    world.resetAtRaidGate();
    const BaseFacility &gate = *std::find_if(
        world.facilities().begin(), world.facilities().end(),
        [](const BaseFacility &facility)
        { return facility.kind == BaseFacilityKind::RaidGate; });
    BaseInput moveUpFast;
    moveUpFast.moveUp = true;
    moveUpFast.sprint = true;
    static_cast<void>(world.update(moveUpFast, 2.0F));
    EXPECT_FLOAT_EQ(
        world.playerPosition().y,
        gate.bounds.position.y + gate.bounds.size.y);
}

TEST(BaseWorldTest, ExposesLargeHomeRegionAndChunkedPresentation)
{
    BaseWorld world;
    EXPECT_FLOAT_EQ(world.worldSize().x, 12800.0F);
    EXPECT_FLOAT_EQ(world.worldSize().y, 7200.0F);
    EXPECT_LT(world.baseParcel().size.x * world.baseParcel().size.y,
              world.worldSize().x * world.worldSize().y * 0.03F);

    const Vec2 player = world.playerPosition();
    const HomeRegionPresentationProjection &first =
        world.outdoorPresentation({player, {1280.0F, 720.0F}});
    EXPECT_GT(first.queriedChunkCount, 0U);
    EXPECT_LT(first.props.size(), world.layout().props.size());
    const std::uint64_t revision = first.cacheRevision;
    const HomeRegionPresentationProjection &cached =
        world.outdoorPresentation({player, {1280.0F, 720.0F}});
    EXPECT_EQ(cached.cacheRevision, revision);
}

TEST(BaseWorldTest, ChangingMainBaseSiteChangesStableLayout)
{
    BaseWorld world;
    const std::uint64_t greyline = world.layout().layoutHash;
    world.configureSite("regional_base_site.ashworks_logistics_yard");
    const std::uint64_t ashworks = world.layout().layoutHash;
    EXPECT_NE(greyline, ashworks);
    EXPECT_EQ(world.layout().siteDefinitionId,
              "regional_base_site.ashworks_logistics_yard");
    const Vec2 firstSpawn = world.playerPosition();
    world.configureSite("regional_base_site.ashworks_logistics_yard");
    EXPECT_EQ(world.layout().layoutHash, ashworks);
    EXPECT_FLOAT_EQ(world.playerPosition().x, firstSpawn.x);
    EXPECT_FLOAT_EQ(world.playerPosition().y, firstSpawn.y);
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
