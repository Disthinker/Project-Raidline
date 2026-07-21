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