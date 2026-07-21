#include <gtest/gtest.h>

#include <type_traits>
#include <utility>

#include "ground_item.h"

static_assert(
    !std::is_copy_constructible_v<GroundItem>);

static_assert(
    !std::is_copy_assignable_v<GroundItem>);

static_assert(
    std::is_move_constructible_v<GroundItem>);

static_assert(
    std::is_move_assignable_v<GroundItem>);

TEST(
    GroundItemTest,
    StoresItemAndCenterPosition)
{
    const GroundItem groundItem{
        ItemInstance{
            11,
            ItemId::Cola},
        Vec2{
            100.0f,
            200.0f}};

    EXPECT_EQ(
        groundItem.item().instanceId(),
        11u);

    EXPECT_EQ(
        groundItem.item().definitionId(),
        ItemId::Cola);

    EXPECT_FLOAT_EQ(
        groundItem.position().x,
        100.0f);

    EXPECT_FLOAT_EQ(
        groundItem.position().y,
        200.0f);
}

TEST(
    GroundItemTest,
    ColaPickupBoundsUseCenterPosition)
{
    const GroundItem groundItem{
        ItemInstance{
            11,
            ItemId::Cola},
        Vec2{
            100.0f,
            200.0f}};

    const Rect bounds =
        groundItem.pickupBounds();

    // Cola pickupSize 是 48×48，
    // 中心点 (100, 200) 转为左上角 (76, 176)。
    EXPECT_FLOAT_EQ(
        bounds.position.x,
        76.0f);

    EXPECT_FLOAT_EQ(
        bounds.position.y,
        176.0f);

    EXPECT_FLOAT_EQ(
        bounds.size.x,
        48.0f);

    EXPECT_FLOAT_EQ(
        bounds.size.y,
        48.0f);
}

TEST(
    GroundItemTest,
    RiflePickupBoundsUseDefinitionSize)
{
    const GroundItem groundItem{
        ItemInstance{
            21,
            ItemId::Rifle},
        Vec2{
            500.0f,
            300.0f}};

    const Rect bounds =
        groundItem.pickupBounds();

    // Rifle pickupSize 是 136×72。
    EXPECT_FLOAT_EQ(
        bounds.position.x,
        432.0f);

    EXPECT_FLOAT_EQ(
        bounds.position.y,
        264.0f);

    EXPECT_FLOAT_EQ(
        bounds.size.x,
        136.0f);

    EXPECT_FLOAT_EQ(
        bounds.size.y,
        72.0f);
}

TEST(
    GroundItemTest,
    TakingItemPreservesInstanceIdentity)
{
    GroundItem groundItem{
        ItemInstance{
            42,
            ItemId::Pistol},
        Vec2{
            300.0f,
            250.0f}};

    ItemInstance carriedItem =
        groundItem.takeItem();

    EXPECT_TRUE(carriedItem.valid());
    EXPECT_EQ(
        carriedItem.instanceId(),
        42u);
    EXPECT_EQ(
        carriedItem.definitionId(),
        ItemId::Pistol);

    // 原 GroundItem 不再拥有有效物品。
    EXPECT_FALSE(
        groundItem.item().valid());
}

TEST(
    GroundItemTest,
    MovingGroundItemPreservesOwnedIdentity)
{
    GroundItem source{
        ItemInstance{
            75,
            ItemId::Medkit},
        Vec2{
            250.0f,
            400.0f}};

    GroundItem destination{
        std::move(source)};

    EXPECT_EQ(
        destination.item().instanceId(),
        75u);

    EXPECT_EQ(
        destination.item().definitionId(),
        ItemId::Medkit);

    EXPECT_FLOAT_EQ(
        destination.position().x,
        250.0f);

    EXPECT_FLOAT_EQ(
        destination.position().y,
        400.0f);

    EXPECT_FALSE(
        source.item().valid());
}
