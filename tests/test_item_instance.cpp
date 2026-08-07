#include <gtest/gtest.h>

#include <stdexcept>
#include <type_traits>
#include <utility>

#include "item_instance.h"

static_assert(
    !std::is_copy_constructible_v<ItemInstance>);

static_assert(
    !std::is_copy_assignable_v<ItemInstance>);

static_assert(
    std::is_move_constructible_v<ItemInstance>);

static_assert(
    std::is_move_assignable_v<ItemInstance>);

TEST(
    ItemInstanceTest,
    StoresUniqueIdentityAndDefinition)
{
    const ItemInstance item{
        42,
        ItemId::Rifle};

    EXPECT_TRUE(item.valid());
    EXPECT_EQ(item.instanceId(), 42u);
    EXPECT_EQ(
        item.definitionId(),
        ItemId::Rifle);
}

TEST(
    ItemInstanceTest,
    RejectsZeroInstanceId)
{
    EXPECT_THROW(
        (ItemInstance{
            0,
            ItemId::Cola}),
        std::invalid_argument);
}

TEST(
    ItemInstanceTest,
    RejectsCountAsDefinitionId)
{
    EXPECT_THROW(
        (ItemInstance{
            1,
            ItemId::Count}),
        std::out_of_range);
}

TEST(
    ItemInstanceTest,
    RejectsUnknownDefinitionId)
{
    const ItemId invalidId =
        static_cast<ItemId>(999);

    EXPECT_THROW(
        (ItemInstance{
            1,
            invalidId}),
        std::out_of_range);
}

TEST(
    ItemInstanceTest,
    MoveConstructionTransfersIdentity)
{
    ItemInstance source{
        42,
        ItemId::Pistol};

    ItemInstance destination{
        std::move(source)};

    EXPECT_TRUE(destination.valid());
    EXPECT_EQ(
        destination.instanceId(),
        42u);
    EXPECT_EQ(
        destination.definitionId(),
        ItemId::Pistol);

    EXPECT_FALSE(source.valid());
    EXPECT_EQ(source.instanceId(), 0u);
    EXPECT_EQ(
        source.definitionId(),
        ItemId::Count);
}

TEST(
    ItemInstanceTest,
    MoveAssignmentTransfersIdentity)
{
    ItemInstance source{
        42,
        ItemId::Medkit};

    ItemInstance destination{
        7,
        ItemId::Cola};

    destination =
        std::move(source);

    EXPECT_TRUE(destination.valid());
    EXPECT_EQ(
        destination.instanceId(),
        42u);
    EXPECT_EQ(
        destination.definitionId(),
        ItemId::Medkit);

    EXPECT_FALSE(source.valid());
}

TEST(
    ItemInstanceTest,
    SelfMoveAssignmentKeepsIdentity)
{
    ItemInstance item{
        42,
        ItemId::Rifle};

    ItemInstance *itemPointer =
        &item;

    *itemPointer =
        std::move(*itemPointer);

    EXPECT_TRUE(item.valid());
    EXPECT_EQ(item.instanceId(), 42u);
    EXPECT_EQ(
        item.definitionId(),
        ItemId::Rifle);
}

TEST(ItemInstanceTest, StartsInDefaultOrientation)
{
    const ItemInstance item{43, ItemId::Rifle};

    EXPECT_EQ(
        item.orientation(),
        ItemOrientation::Degrees0);
}

TEST(ItemInstanceTest, ValidRotationSurvivesMoveConstruction)
{
    ItemInstance source{44, ItemId::Rifle};
    ASSERT_TRUE(source.trySetOrientation(ItemOrientation::Degrees90));

    ItemInstance destination{std::move(source)};

    EXPECT_EQ(
        destination.orientation(),
        ItemOrientation::Degrees90);
    EXPECT_FALSE(source.valid());
    EXPECT_EQ(
        source.orientation(),
        ItemOrientation::Degrees0);
}

TEST(ItemInstanceTest, NonRotatableItemRejectsOrientationChange)
{
    ItemInstance medkit{45, ItemId::Medkit};

    EXPECT_FALSE(
        medkit.trySetOrientation(ItemOrientation::Degrees90));
    EXPECT_EQ(
        medkit.orientation(),
        ItemOrientation::Degrees0);
}

TEST(ItemInstanceTest, StoresValidatedStackQuantity)
{
    const ItemInstance ammo{46, ItemId::Ammo9mm, 37};

    EXPECT_TRUE(ammo.valid());
    EXPECT_EQ(ammo.quantity(), 37U);
}

TEST(ItemInstanceTest, RejectsZeroAndOverMaximumQuantity)
{
    EXPECT_THROW(
        (ItemInstance{47, ItemId::Ammo9mm, 0}),
        std::invalid_argument);
    EXPECT_THROW(
        (ItemInstance{48, ItemId::Ammo9mm, 61}),
        std::invalid_argument);
    EXPECT_THROW(
        (ItemInstance{49, ItemId::Cola, 2}),
        std::invalid_argument);
}

TEST(ItemInstanceTest, QuantityMutationRespectsDefinitionMaximum)
{
    ItemInstance ammo{50, ItemId::Ammo9mm, 10};

    EXPECT_TRUE(ammo.trySetQuantity(60));
    EXPECT_EQ(ammo.quantity(), 60U);
    EXPECT_FALSE(ammo.trySetQuantity(0));
    EXPECT_FALSE(ammo.trySetQuantity(61));
    EXPECT_EQ(ammo.quantity(), 60U);
}

TEST(ItemInstanceTest, MoveTransfersQuantityAndClearsSource)
{
    ItemInstance source{51, ItemId::Ammo9mm, 23};
    ItemInstance destination{std::move(source)};

    EXPECT_EQ(destination.quantity(), 23U);
    EXPECT_EQ(source.quantity(), 0U);
    EXPECT_FALSE(source.valid());
}
