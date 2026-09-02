#include <gtest/gtest.h>

#include <algorithm>

#include "base_world.h"
#include "item_definition.h"

namespace
{
bool overlaps(const ContentRect &left, const ContentRect &right)
{
    return left.position.x < right.position.x + right.size.x &&
        left.position.x + left.size.x > right.position.x &&
        left.position.y < right.position.y + right.size.y &&
        left.position.y + left.size.y > right.position.y;
}

bool inside(const ContentRect &inner, const ContentRect &outer)
{
    return inner.position.x >= outer.position.x &&
        inner.position.y >= outer.position.y &&
        inner.position.x + inner.size.x <=
            outer.position.x + outer.size.x &&
        inner.position.y + inner.size.y <=
            outer.position.y + outer.size.y;
}
}

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

TEST(BaseWorldTest, PlacedStorageBlockerStopsMovementAndAllowsSliding)
{
    BaseWorld world;
    const Vec2 start = world.playerPosition();
    world.configureGroundBlockers({ContentRect{
        {start.x - 120.0F, start.y - 180.0F},
        {100.0F, 420.0F}}});

    BaseInput moveLeft;
    moveLeft.moveLeft = true;
    for (int index{}; index < 30; ++index)
        static_cast<void>(world.update(moveLeft, 0.05F));
    EXPECT_GE(world.playerPosition().x, start.x - 20.0F);

    const float beforeY = world.playerPosition().y;
    BaseInput slide;
    slide.moveLeft = true;
    slide.moveUp = true;
    for (int index{}; index < 8; ++index)
        static_cast<void>(world.update(slide, 0.05F));
    EXPECT_GE(world.playerPosition().x, start.x - 20.0F);
    EXPECT_LT(world.playerPosition().y, beforeY);
}

TEST(BaseWorldTest, SharedShootingProducesAimTracerFeedbackAndWorldImpact)
{
    BaseWorld world;
    const ItemDefinition &rifle = itemDefinition(ItemId::Rifle);
    ASSERT_TRUE(rifle.weaponUse.has_value());
    world.configureWeaponFire(*rifle.weaponUse);

    const Vec2 player = world.playerPosition();
    const float horizontalDirection = player.x < world.worldSize().x * 0.5F
        ? 1.0F : -1.0F;
    GameplayInput fire{};
    fire.aimWorldPosition = Vec2{
        player.x + horizontalDirection * 900.0F, player.y};
    fire.fireJustPressed = true;
    fire.firePressed = true;
    static_cast<void>(world.update(fire, 1.0F / 60.0F));

    EXPECT_TRUE(world.shotFiredLastUpdate());
    EXPECT_GT(
        world.weaponAimDirection().x * horizontalDirection,
        0.0F);
    EXPECT_FALSE(world.shotFeedbackPresentationSnapshots().empty());

    bool resolved{};
    for (int step{}; step < 20 && !resolved; ++step)
    {
        static_cast<void>(world.update(GameplayInput{}, 0.05F));
        resolved = !world.hitResultsLastUpdate().empty();
    }
    ASSERT_TRUE(resolved);
    EXPECT_NE(
        world.hitResultsLastUpdate().front().targetKind,
        HitTargetKind::Enemy);
    EXPECT_FALSE(world.particles().empty());
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

TEST(BaseWorldTest, StashConnectionExistsOnlyInsideBaseParcel)
{
    BaseWorld world;
    ASSERT_TRUE(world.canAccessStash());

    BaseInput leaveBase;
    leaveBase.moveDown = true;
    for (int step{}; step < 30 && world.canAccessStash(); ++step)
    {
        static_cast<void>(world.update(leaveBase, 0.1F));
    }

    EXPECT_FALSE(world.canAccessStash());
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
    ASSERT_EQ(world.facilities().size(), 8U);
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

    BaseInput moveDown;
    moveDown.moveDown = true;
    for (int index{}; index < 8; ++index)
    {
        static_cast<void>(world.update(moveDown, 0.05F));
    }
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

    BaseInput moveDown;
    moveDown.moveDown = true;
    for (int index{}; index < 8; ++index)
    {
        static_cast<void>(world.update(moveDown, 0.05F));
    }
    BaseInput moveRight;
    moveRight.moveRight = true;
    for (int index{}; index < 58; ++index)
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

TEST(BaseWorldTest, FacilitySideDoesNotReplaceThePublishedEntrance)
{
    BaseWorld world;
    const Vec2 playerCenter{
        world.playerPosition().x + world.playerSize().x * 0.5F,
        world.playerPosition().y + world.playerSize().y * 0.5F};
    world.configureSite(
        world.siteDefinitionId(),
        {BaseFacilitySpatialOverride{
            BaseFacilityKind::Storage,
            {playerCenter.x + 190.0F, playerCenter.y},
            true}});
    EXPECT_FALSE(world.interactableFacility().has_value());

    const auto storage = std::find_if(
        world.facilities().begin(), world.facilities().end(),
        [](const BaseFacility &facility)
        { return facility.kind == BaseFacilityKind::Storage; });
    ASSERT_NE(storage, world.facilities().end());
    const BaseFacilityAccessGeometry geometry =
        baseFacilityAccessGeometry(*storage);
    EXPECT_GT(
        geometry.entrancePoint.y,
        storage->bounds.position.y + storage->bounds.size.y);
}

TEST(BaseWorldTest, ActiveFacilitiesPublishReservedEntranceWorkZones)
{
    const BaseWorld world;
    const std::vector<ContentRect> blockers = world.basePlacementBlockers();
    std::size_t socketCount{};
    for (const BaseFacility &facility : world.facilities())
    {
        if (!facility.active)
            continue;
        const BaseFacilityAccessGeometry geometry =
            baseFacilityAccessGeometry(facility);
        EXPECT_FLOAT_EQ(
            geometry.entrancePoint.x,
            facility.bounds.position.x + facility.bounds.size.x * 0.5F);
        EXPECT_GT(
            geometry.entrancePoint.y,
            facility.bounds.position.y + facility.bounds.size.y);
        EXPECT_TRUE(std::any_of(
            blockers.begin(), blockers.end(),
            [&](const ContentRect &bounds)
            {
                return bounds.position.x == geometry.workZone.position.x &&
                    bounds.position.y == geometry.workZone.position.y &&
                    bounds.size.x == geometry.workZone.size.x &&
                    bounds.size.y == geometry.workZone.size.y;
            }));
        EXPECT_TRUE(inside(geometry.workZone, world.baseParcel()));
        if (const auto socket = baseFacilityWorkSocket(facility);
            socket.has_value())
        {
            ++socketCount;
            EXPECT_TRUE(inside(socket->bounds, geometry.workZone));
        }
        for (const ContentRect &environment : world.layout().movementBlockers)
            EXPECT_FALSE(overlaps(geometry.workZone, environment));
        for (const BaseFacility &other : world.facilities())
        {
            if (!other.active || other.kind == facility.kind)
                continue;
            EXPECT_FALSE(overlaps(
                geometry.workZone,
                ContentRect{other.bounds.position, other.bounds.size}));
            EXPECT_FALSE(overlaps(
                geometry.workZone,
                baseFacilityAccessGeometry(other).workZone));
        }
    }
    EXPECT_EQ(socketCount, 4U);
}

TEST(BaseWorldTest, KitchenWaterRequiresOwnedSpatialActivation)
{
    BaseWorld world;
    const auto initial = std::find_if(
        world.facilities().begin(), world.facilities().end(),
        [](const BaseFacility &facility)
        { return facility.kind == BaseFacilityKind::KitchenWater; });
    ASSERT_NE(initial, world.facilities().end());
    EXPECT_FALSE(initial->active);
    EXPECT_STREQ(
        baseFacilityName(BaseFacilityKind::KitchenWater),
        "KITCHEN & WATER");

    const Vec2 center{5100.0F, 3200.0F};
    world.configureSite(
        "regional_base_site.greyline_yard",
        {BaseFacilitySpatialOverride{
            BaseFacilityKind::KitchenWater, center, true}});
    const auto installed = std::find_if(
        world.facilities().begin(), world.facilities().end(),
        [](const BaseFacility &facility)
        { return facility.kind == BaseFacilityKind::KitchenWater; });
    ASSERT_NE(installed, world.facilities().end());
    EXPECT_TRUE(installed->active);
    EXPECT_FLOAT_EQ(
        installed->bounds.position.x + installed->bounds.size.x * 0.5F,
        center.x);
    EXPECT_FLOAT_EQ(
        installed->bounds.position.y + installed->bounds.size.y * 0.5F,
        center.y);
}

TEST(BaseWorldTest, SpatialOverridesMoveCoreFacilityAndCollisionTogether)
{
    BaseWorld world;
    const Vec2 center{5200.0F, 3300.0F};
    world.configureSite(
        "regional_base_site.greyline_yard",
        {BaseFacilitySpatialOverride{BaseFacilityKind::Storage, center}});
    const auto storage = std::find_if(
        world.facilities().begin(), world.facilities().end(),
        [](const BaseFacility &facility)
        { return facility.kind == BaseFacilityKind::Storage; });
    ASSERT_NE(storage, world.facilities().end());
    EXPECT_FLOAT_EQ(
        storage->bounds.position.x + storage->bounds.size.x * 0.5F,
        center.x);
    EXPECT_FLOAT_EQ(
        storage->bounds.position.y + storage->bounds.size.y * 0.5F,
        center.y);

    const auto excluding = world.basePlacementBlockersExcluding(
        BaseFacilityKind::Storage);
    EXPECT_FALSE(std::any_of(
        excluding.begin(), excluding.end(),
        [&](const ContentRect &bounds)
        { return bounds.position.x == storage->bounds.position.x &&
                 bounds.position.y == storage->bounds.position.y &&
                 bounds.size.x == storage->bounds.size.x &&
                 bounds.size.y == storage->bounds.size.y; }));
}

TEST(BaseWorldTest, ReserveFacilityIsNotVisibleOrBlocking)
{
    BaseWorld world;
    const Vec2 playerBefore = world.playerPosition();
    const Vec2 center{5200.0F, 3300.0F};
    world.configureSite(
        "regional_base_site.greyline_yard",
        {BaseFacilitySpatialOverride{
            BaseFacilityKind::Workshop, center, false}});
    const auto workshop = std::find_if(
        world.facilities().begin(), world.facilities().end(),
        [](const BaseFacility &facility)
        { return facility.kind == BaseFacilityKind::Workshop; });
    ASSERT_NE(workshop, world.facilities().end());
    EXPECT_FALSE(workshop->active);
    EXPECT_FLOAT_EQ(world.playerPosition().x, playerBefore.x);
    EXPECT_FLOAT_EQ(world.playerPosition().y, playerBefore.y);
    const auto blockers = world.basePlacementBlockers();
    EXPECT_FALSE(std::any_of(
        blockers.begin(), blockers.end(),
        [&](const ContentRect &bounds)
        { return bounds.position.x == workshop->bounds.position.x &&
                 bounds.position.y == workshop->bounds.position.y &&
                 bounds.size.x == workshop->bounds.size.x &&
                 bounds.size.y == workshop->bounds.size.y; }));
    const BaseFacilityAccessGeometry access =
        baseFacilityAccessGeometry(*workshop);
    EXPECT_FALSE(std::any_of(
        blockers.begin(), blockers.end(),
        [&](const ContentRect &bounds)
        {
            return bounds.position.x == access.workZone.position.x &&
                bounds.position.y == access.workZone.position.y &&
                bounds.size.x == access.workZone.size.x &&
                bounds.size.y == access.workZone.size.y;
        }));
}
