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