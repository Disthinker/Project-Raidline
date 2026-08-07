#include <gtest/gtest.h>

#include <optional>
#include <utility>
#include <vector>

#include "inventory_transfer.h"

namespace
{

    struct PlacedSnapshot
    {
        ItemInstanceId instanceId{};
        ItemId definitionId{ItemId::Count};
        GridPosition origin{};

        friend bool operator==(
            const PlacedSnapshot &,
            const PlacedSnapshot &) = default;
    };

    struct InventorySnapshot
    {
        std::vector<PlacedSnapshot> placedItems;
        std::vector<std::optional<ItemInstanceId>> cells;

        friend bool operator==(
            const InventorySnapshot &,
            const InventorySnapshot &) = default;
    };

    InventorySnapshot snapshot(
        const GridInventory &inventory)
    {
        InventorySnapshot result;
        result.placedItems.reserve(
            inventory.placedItems().size());
        result.cells.reserve(
            inventory.cellCount());

        for (const PlacedItem &placed : inventory.placedItems())
        {
            result.placedItems.push_back({
                placed.item.instanceId(),
                placed.item.definitionId(),
                placed.origin,
            });
        }

        for (int y = 0; y < inventory.height(); ++y)
        {
            for (int x = 0; x < inventory.width(); ++x)
            {
                result.cells.push_back(
                    inventory.occupantAt({x, y}));
            }
        }

        return result;
    }

    void place(
        GridInventory &inventory,
        ItemInstanceId instanceId,
        ItemId definitionId,
        GridPosition origin)
    {
        ItemInstance item{instanceId, definitionId};
        ASSERT_TRUE(
            inventory.tryPlace(
                std::move(item),
                origin));
    }

} // namespace

TEST(InventoryTransferTest, TransfersMultiCellItemAtRequestedOrigin)
{
    GridInventory source{{10, 6}};
    GridInventory destination{{6, 6}};
    place(source, 101, ItemId::Rifle, {1, 2});

    ASSERT_TRUE(
        tryTransferItem(
            source,
            destination,
            101,
            {2, 3}));

    EXPECT_TRUE(source.placedItems().empty());
    ASSERT_EQ(destination.placedItems().size(), 1U);

    const PlacedItem &placed =
        destination.placedItems().front();
    EXPECT_EQ(placed.item.instanceId(), 101U);
    EXPECT_EQ(placed.item.definitionId(), ItemId::Rifle);
    EXPECT_EQ(placed.origin, (GridPosition{2, 3}));

    for (int y = 3; y < 5; ++y)
    {
        for (int x = 2; x < 6; ++x)
        {
            EXPECT_EQ(
                destination.occupantAt({x, y}),
                (std::optional<ItemInstanceId>{101}));
        }
    }
}

TEST(InventoryTransferTest, QueryDoesNotMutateEitherInventory)
{
    GridInventory source{{10, 6}};
    GridInventory destination{{6, 6}};
    place(source, 102, ItemId::Medkit, {3, 1});

    const InventorySnapshot sourceBefore = snapshot(source);
    const InventorySnapshot destinationBefore = snapshot(destination);

    EXPECT_TRUE(
        canTransferItem(
            source,
            destination,
            102,
            {2, 2}));
    EXPECT_EQ(snapshot(source), sourceBefore);
    EXPECT_EQ(snapshot(destination), destinationBefore);
}

TEST(InventoryTransferTest, InvalidDestinationLeavesBothInventoriesUnchanged)
{
    GridInventory source{{10, 6}};
    GridInventory destination{{6, 6}};
    place(source, 103, ItemId::Rifle, {1, 1});

    const InventorySnapshot sourceBefore = snapshot(source);
    const InventorySnapshot destinationBefore = snapshot(destination);

    EXPECT_FALSE(
        tryTransferItem(
            source,
            destination,
            103,
            {3, 5}));
    EXPECT_EQ(snapshot(source), sourceBefore);
    EXPECT_EQ(snapshot(destination), destinationBefore);
}

TEST(InventoryTransferTest, OccupiedDestinationLeavesBothInventoriesUnchanged)
{
    GridInventory source{{10, 6}};
    GridInventory destination{{6, 6}};
    place(source, 104, ItemId::Medkit, {1, 1});
    place(destination, 204, ItemId::Pistol, {2, 2});

    const InventorySnapshot sourceBefore = snapshot(source);
    const InventorySnapshot destinationBefore = snapshot(destination);

    EXPECT_FALSE(
        tryTransferItem(
            source,
            destination,
            104,
            {1, 2}));
    EXPECT_EQ(snapshot(source), sourceBefore);
    EXPECT_EQ(snapshot(destination), destinationBefore);
}

TEST(InventoryTransferTest, DuplicateStableIdLeavesBothInventoriesUnchanged)
{
    GridInventory source{{10, 6}};
    GridInventory destination{{6, 6}};
    place(source, 105, ItemId::Cola, {0, 0});
    place(destination, 105, ItemId::Pistol, {3, 3});

    const InventorySnapshot sourceBefore = snapshot(source);
    const InventorySnapshot destinationBefore = snapshot(destination);

    EXPECT_FALSE(
        tryTransferItem(
            source,
            destination,
            105,
            {0, 0}));
    EXPECT_EQ(snapshot(source), sourceBefore);
    EXPECT_EQ(snapshot(destination), destinationBefore);
}

TEST(InventoryTransferTest, MissingIdLeavesBothInventoriesUnchanged)
{
    GridInventory source{{10, 6}};
    GridInventory destination{{6, 6}};
    place(source, 106, ItemId::Cola, {0, 0});

    const InventorySnapshot sourceBefore = snapshot(source);
    const InventorySnapshot destinationBefore = snapshot(destination);

    EXPECT_FALSE(
        tryTransferItem(
            source,
            destination,
            999,
            {1, 1}));
    EXPECT_EQ(snapshot(source), sourceBefore);
    EXPECT_EQ(snapshot(destination), destinationBefore);
}

TEST(InventoryTransferTest, RejectsSameInventoryWithoutMutation)
{
    GridInventory inventory{{10, 6}};
    place(inventory, 107, ItemId::Pistol, {0, 0});
    const InventorySnapshot before = snapshot(inventory);

    EXPECT_FALSE(
        tryTransferItem(
            inventory,
            inventory,
            107,
            {4, 2}));
    EXPECT_EQ(snapshot(inventory), before);
}

TEST(InventoryTransferTest, FirstFitIsRowMajorAndTransfersThere)
{
    GridInventory source{{10, 6}};
    GridInventory destination{{6, 6}};
    place(source, 108, ItemId::Pistol, {0, 0});
    place(destination, 208, ItemId::Cola, {0, 0});

    EXPECT_EQ(
        findFirstTransferFit(
            source,
            destination,
            108),
        (std::optional<GridPosition>{GridPosition{1, 0}}));

    ASSERT_TRUE(
        tryTransferItemFirstFit(
            source,
            destination,
            108));
    EXPECT_EQ(
        destination.originOf(108),
        (std::optional<GridPosition>{GridPosition{1, 0}}));
}

TEST(InventoryTransferTest, FullDestinationRejectsFirstFitWithoutMutation)
{
    GridInventory source{{10, 6}};
    GridInventory destination{{2, 1}};
    place(source, 109, ItemId::Cola, {0, 0});
    place(destination, 209, ItemId::Pistol, {0, 0});

    const InventorySnapshot sourceBefore = snapshot(source);
    const InventorySnapshot destinationBefore = snapshot(destination);

    EXPECT_EQ(
        findFirstTransferFit(source, destination, 109),
        std::nullopt);
    EXPECT_FALSE(
        tryTransferItemFirstFit(source, destination, 109));
    EXPECT_EQ(snapshot(source), sourceBefore);
    EXPECT_EQ(snapshot(destination), destinationBefore);
}
