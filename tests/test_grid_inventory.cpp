#include <gtest/gtest.h>

#include <optional>
#include <stdexcept>
#include <type_traits>

#include "grid_inventory.h"

TEST(GridInventoryTest, ValidDimensionsCreateEmptyGrid)
{
    GridInventory inventory({10, 6});

    EXPECT_EQ(inventory.width(), 10);
    EXPECT_EQ(inventory.height(), 6);
    EXPECT_EQ(inventory.cellCount(), 60U);
    EXPECT_TRUE(inventory.placedItems().empty());

    EXPECT_EQ(
        inventory.occupantAt({0, 0}),
        std::nullopt);

    EXPECT_EQ(
        inventory.occupantAt({9, 5}),
        std::nullopt);
}

TEST(GridInventoryTest, RejectsZeroWidth)
{
    EXPECT_THROW(
        (GridInventory{InventoryGridSize{0, 6}}),
        std::invalid_argument);
}

TEST(GridInventoryTest, RejectsNegativeWidth)
{
    EXPECT_THROW(
        (GridInventory{InventoryGridSize{-1, 6}}),
        std::invalid_argument);
}

TEST(GridInventoryTest, RejectsZeroHeight)
{
    EXPECT_THROW(
        (GridInventory{InventoryGridSize{10, 0}}),
        std::invalid_argument);
}

TEST(GridInventoryTest, RejectsNegativeHeight)
{
    EXPECT_THROW(
        (GridInventory{InventoryGridSize{10, -1}}),
        std::invalid_argument);
}

TEST(GridInventoryTest, OccupantAtRejectsNegativeX)
{
    GridInventory inventory({10, 6});

    EXPECT_EQ(
        inventory.occupantAt({-1, 0}),
        std::nullopt);
}

TEST(GridInventoryTest, OccupantAtRejectsNegativeY)
{
    GridInventory inventory({10, 6});

    EXPECT_EQ(
        inventory.occupantAt({0, -1}),
        std::nullopt);
}

TEST(GridInventoryTest, OccupantAtRejectsRightOverflow)
{
    GridInventory inventory({10, 6});

    EXPECT_EQ(
        inventory.occupantAt({10, 0}),
        std::nullopt);
}

TEST(GridInventoryTest, OccupantAtRejectsBottomOverflow)
{
    GridInventory inventory({10, 6});

    EXPECT_EQ(
        inventory.occupantAt({0, 6}),
        std::nullopt);
}

TEST(GridInventoryTest, EveryValidCellStartsEmpty)
{
    GridInventory inventory({10, 6});

    for (int y = 0; y < inventory.height(); ++y)
    {
        for (int x = 0; x < inventory.width(); ++x)
        {
            EXPECT_EQ(
                inventory.occupantAt({x, y}),
                std::nullopt);
        }
    }
}

TEST(GridInventoryTest, PlacedItemIsMoveOnly)
{
    static_assert(
        !std::is_copy_constructible_v<PlacedItem>);

    static_assert(
        !std::is_copy_assignable_v<PlacedItem>);

    static_assert(
        std::is_move_constructible_v<PlacedItem>);

    static_assert(
        std::is_move_assignable_v<PlacedItem>);

    SUCCEED();
}

TEST(GridInventoryTest, CanPlaceOneByOneAtOrigin)
{
    GridInventory inventory({10, 6});

    EXPECT_TRUE(
        inventory.canPlace(
            ItemId::Cola,
            GridPosition{0, 0}));
}

TEST(GridInventoryTest, CanPlaceMultiCellItemInsideBounds)
{
    GridInventory inventory({10, 6});

    EXPECT_TRUE(
        inventory.canPlace(
            ItemId::Medkit,
            GridPosition{4, 2}));

    EXPECT_TRUE(
        inventory.canPlace(
            ItemId::Rifle,
            GridPosition{3, 1}));
}

TEST(GridInventoryTest, CanPlaceItemsAtExactBottomRightBoundary)
{
    GridInventory inventory({10, 6});

    // Medkit 为 2×2，最大合法 origin 是 (8, 4)。
    EXPECT_TRUE(
        inventory.canPlace(
            ItemId::Medkit,
            GridPosition{8, 4}));

    // Rifle 为 4×2，最大合法 origin 是 (6, 4)。
    EXPECT_TRUE(
        inventory.canPlace(
            ItemId::Rifle,
            GridPosition{6, 4}));
}

TEST(GridInventoryTest, RejectsNegativePlacementOrigin)
{
    GridInventory inventory({10, 6});

    EXPECT_FALSE(
        inventory.canPlace(
            ItemId::Cola,
            GridPosition{-1, 0}));

    EXPECT_FALSE(
        inventory.canPlace(
            ItemId::Cola,
            GridPosition{0, -1}));
}

TEST(GridInventoryTest, RejectsRightOverflow)
{
    GridInventory inventory({10, 6});

    // Rifle 宽度为 4，origin.x 为 7 时会覆盖到第 11 列。
    EXPECT_FALSE(
        inventory.canPlace(
            ItemId::Rifle,
            GridPosition{7, 0}));
}

TEST(GridInventoryTest, RejectsBottomOverflow)
{
    GridInventory inventory({10, 6});

    // Rifle 高度为 2，origin.y 为 5 时会越过底部。
    EXPECT_FALSE(
        inventory.canPlace(
            ItemId::Rifle,
            GridPosition{0, 5}));
}

TEST(GridInventoryTest, RejectsItemLargerThanInventory)
{
    GridInventory inventory({3, 1});

    EXPECT_FALSE(
        inventory.canPlace(
            ItemId::Rifle,
            GridPosition{0, 0}));
}

TEST(GridInventoryTest, CanPlaceAdjacentEmptyRegions)
{
    GridInventory inventory({10, 6});

    // 目前背包为空，因此两个相邻候选区域都分别合法。
    EXPECT_TRUE(
        inventory.canPlace(
            ItemId::Pistol,
            GridPosition{0, 0}));

    EXPECT_TRUE(
        inventory.canPlace(
            ItemId::Pistol,
            GridPosition{2, 0}));
}

TEST(GridInventoryTest, InvalidDefinitionThrows)
{
    GridInventory inventory({10, 6});

    EXPECT_THROW(
        inventory.canPlace(
            ItemId::Count,
            GridPosition{0, 0}),
        std::out_of_range);
}

TEST(GridInventoryTest, EmptyInventoryReturnsTopLeft)
{
    GridInventory inventory({10, 6});

    const auto result =
        inventory.findFirstFit(ItemId::Rifle);

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result.value(), (GridPosition{0, 0}));
}

TEST(GridInventoryTest, FindFirstFitReturnsNulloptWhenItemCannotFit)
{
    GridInventory inventory({3, 1});

    EXPECT_EQ(
        inventory.findFirstFit(ItemId::Rifle),
        std::nullopt);
}

TEST(GridInventoryTest, FindFirstFitIsDeterministic)
{
    GridInventory inventory({10, 6});

    const auto first =
        inventory.findFirstFit(ItemId::Medkit);
    const auto second =
        inventory.findFirstFit(ItemId::Medkit);
    const auto third =
        inventory.findFirstFit(ItemId::Medkit);

    ASSERT_TRUE(first.has_value());
    EXPECT_EQ(first, second);
    EXPECT_EQ(second, third);
    EXPECT_EQ(first.value(), (GridPosition{0, 0}));
}

TEST(GridInventoryTest, FindFirstFitRejectsInvalidDefinition)
{
    GridInventory inventory({10, 6});

    EXPECT_THROW(
        inventory.findFirstFit(ItemId::Count),
        std::out_of_range);
}