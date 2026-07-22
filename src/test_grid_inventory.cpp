#include <gtest/gtest.h>

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

TEST(GridInventoryTest, RejectsNonPositiveWidth)
{
    EXPECT_THROW(
        (GridInventory{InventoryGridSize{0, 6}}),
        std::invalid_argument);

    EXPECT_THROW(
        (GridInventory{InventoryGridSize{-1, 6}}),
        std::invalid_argument);
}

TEST(GridInventoryTest, RejectsNonPositiveHeight)
{
    EXPECT_THROW(
        (GridInventory{InventoryGridSize{10, 0}}),
        std::invalid_argument);

    EXPECT_THROW(
        (GridInventory{InventoryGridSize{10, -1}}),
        std::invalid_argument);
}

TEST(GridInventoryTest, OccupantAtRejectsOutOfBoundsPositions)
{
    GridInventory inventory({10, 6});

    EXPECT_EQ(inventory.occupantAt({-1, 0}), std::nullopt);
    EXPECT_EQ(inventory.occupantAt({0, -1}), std::nullopt);
    EXPECT_EQ(inventory.occupantAt({10, 0}), std::nullopt);
    EXPECT_EQ(inventory.occupantAt({0, 6}), std::nullopt);
}