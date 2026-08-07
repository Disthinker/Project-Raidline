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
        ItemOrientation orientation{ItemOrientation::Degrees0};
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
                placed.item.orientation(),
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

TEST(InventoryTransferTest, CellFirstFitResolvesMultiCellInterior)
{
    GridInventory source{{10, 6}};
    GridInventory destination{{6, 6}};
    place(source, 110, ItemId::Rifle, {1, 1});

    ASSERT_TRUE(
        tryTransferItemAtCellFirstFit(
            source,
            destination,
            {3, 2}));

    EXPECT_TRUE(source.placedItems().empty());
    EXPECT_EQ(
        destination.originOf(110),
        (std::optional<GridPosition>{GridPosition{0, 0}}));
}

TEST(InventoryTransferTest, EmptySourceCellLeavesBothInventoriesUnchanged)
{
    GridInventory source{{10, 6}};
    GridInventory destination{{6, 6}};
    place(source, 111, ItemId::Cola, {0, 0});

    const InventorySnapshot sourceBefore = snapshot(source);
    const InventorySnapshot destinationBefore = snapshot(destination);

    EXPECT_FALSE(
        tryTransferItemAtCellFirstFit(
            source,
            destination,
            {5, 5}));
    EXPECT_EQ(snapshot(source), sourceBefore);
    EXPECT_EQ(snapshot(destination), destinationBefore);
}

TEST(InventoryTransferTest, CellFirstFitFullDestinationIsNonMutating)
{
    GridInventory source{{10, 6}};
    GridInventory destination{{2, 1}};
    place(source, 112, ItemId::Cola, {0, 0});
    place(destination, 212, ItemId::Pistol, {0, 0});

    const InventorySnapshot sourceBefore = snapshot(source);
    const InventorySnapshot destinationBefore = snapshot(destination);

    EXPECT_FALSE(
        tryTransferItemAtCellFirstFit(
            source,
            destination,
            {0, 0}));
    EXPECT_EQ(snapshot(source), sourceBefore);
    EXPECT_EQ(snapshot(destination), destinationBefore);
}

TEST(InventoryTransferRotationTest, TransferCommitsRequestedOrientation)
{
    GridInventory source{{10, 6}};
    GridInventory destination{{2, 4}};
    place(source, 120, ItemId::Rifle, {0, 0});

    ASSERT_TRUE(tryTransferItemTransform(
        source,
        destination,
        120,
        {0, 0},
        ItemOrientation::Degrees90));

    EXPECT_TRUE(source.placedItems().empty());
    ASSERT_EQ(destination.placedItems().size(), 1U);
    EXPECT_EQ(
        destination.placedItems().front().item.orientation(),
        ItemOrientation::Degrees90);
    EXPECT_EQ(destination.occupantAt({1, 3}),
              (std::optional<ItemInstanceId>{120}));
}

TEST(InventoryTransferRotationTest, InvalidTransformLeavesBothInventoriesUnchanged)
{
    GridInventory source{{10, 6}};
    GridInventory destination{{2, 3}};
    place(source, 121, ItemId::Rifle, {1, 1});

    const InventorySnapshot sourceBefore = snapshot(source);
    const InventorySnapshot destinationBefore = snapshot(destination);

    EXPECT_FALSE(tryTransferItemTransform(
        source,
        destination,
        121,
        {0, 0},
        ItemOrientation::Degrees90));
    EXPECT_EQ(snapshot(source), sourceBefore);
    EXPECT_EQ(snapshot(destination), destinationBefore);
}

TEST(InventoryTransferRotationTest, FirstFitPreservesExistingOrientation)
{
    GridInventory source{{10, 6}};
    GridInventory destination{{2, 4}};
    ItemInstance rifle{122, ItemId::Rifle};
    ASSERT_TRUE(rifle.trySetOrientation(ItemOrientation::Degrees90));
    ASSERT_TRUE(source.tryPlace(std::move(rifle), {0, 0}));

    ASSERT_TRUE(tryTransferItemFirstFit(
        source,
        destination,
        122));
    ASSERT_EQ(destination.placedItems().size(), 1U);
    EXPECT_EQ(
        destination.placedItems().front().item.orientation(),
        ItemOrientation::Degrees90);
}

TEST(InventoryStackTransferTest, PartialTransferCreatesNewStableId)
{
    GridInventory source{{2, 1}};
    GridInventory destination{{2, 1}};
    ItemInstance ammo{300, ItemId::Ammo9mm, 10};
    ASSERT_TRUE(source.tryPlace(std::move(ammo), {0, 0}));

    const QuantityTransferResult result =
        tryTransferItemQuantityFirstFit(
            source,
            destination,
            300,
            4,
            900);

    EXPECT_TRUE(result.succeeded);
    EXPECT_TRUE(result.consumedSplitInstanceId);
    ASSERT_EQ(source.placedItems().size(), 1U);
    ASSERT_EQ(destination.placedItems().size(), 1U);
    EXPECT_EQ(source.placedItems().front().item.instanceId(), 300U);
    EXPECT_EQ(source.placedItems().front().item.quantity(), 6U);
    EXPECT_EQ(destination.placedItems().front().item.instanceId(), 900U);
    EXPECT_EQ(destination.placedItems().front().item.quantity(), 4U);
}

TEST(InventoryStackTransferTest, MergePreservesDestinationIdWithoutUsingSplitId)
{
    GridInventory source{{2, 1}};
    GridInventory destination{{1, 1}};
    ItemInstance sourceAmmo{301, ItemId::Ammo9mm, 10};
    ItemInstance destinationAmmo{401, ItemId::Ammo9mm, 59};
    ASSERT_TRUE(source.tryPlace(std::move(sourceAmmo), {0, 0}));
    ASSERT_TRUE(destination.tryPlace(std::move(destinationAmmo), {0, 0}));

    const QuantityTransferResult result =
        tryTransferItemQuantityFirstFit(
            source,
            destination,
            301,
            1,
            901);

    EXPECT_TRUE(result.succeeded);
    EXPECT_FALSE(result.consumedSplitInstanceId);
    EXPECT_EQ(source.placedItems().front().item.quantity(), 9U);
    EXPECT_EQ(destination.placedItems().front().item.instanceId(), 401U);
    EXPECT_EQ(destination.placedItems().front().item.quantity(), 60U);
}

TEST(InventoryStackTransferTest, FillsStacksInPlacementOrderThenUsesRowMajorFit)
{
    GridInventory source{{1, 1}};
    GridInventory destination{{4, 1}};
    ItemInstance sourceAmmo{302, ItemId::Ammo9mm, 5};
    ItemInstance first{402, ItemId::Ammo9mm, 58};
    ItemInstance second{403, ItemId::Ammo9mm, 59};
    ASSERT_TRUE(source.tryPlace(std::move(sourceAmmo), {0, 0}));
    ASSERT_TRUE(destination.tryPlace(std::move(first), {1, 0}));
    ASSERT_TRUE(destination.tryPlace(std::move(second), {3, 0}));

    const QuantityTransferResult result =
        tryTransferItemQuantityFirstFit(
            source,
            destination,
            302,
            5,
            902);

    EXPECT_TRUE(result.succeeded);
    EXPECT_FALSE(result.consumedSplitInstanceId);
    EXPECT_TRUE(source.placedItems().empty());
    ASSERT_EQ(destination.placedItems().size(), 3U);
    EXPECT_EQ(destination.placedItems()[0].item.quantity(), 60U);
    EXPECT_EQ(destination.placedItems()[1].item.quantity(), 60U);
    EXPECT_EQ(destination.placedItems()[2].item.instanceId(), 302U);
    EXPECT_EQ(destination.placedItems()[2].item.quantity(), 2U);
    EXPECT_EQ(destination.placedItems()[2].origin, (GridPosition{0, 0}));
}

TEST(InventoryStackTransferTest, InsufficientCapacityLeavesBothInventoriesUnchanged)
{
    GridInventory source{{1, 1}};
    GridInventory destination{{1, 1}};
    ItemInstance sourceAmmo{303, ItemId::Ammo9mm, 10};
    ItemInstance destinationAmmo{403, ItemId::Ammo9mm, 58};
    ASSERT_TRUE(source.tryPlace(std::move(sourceAmmo), {0, 0}));
    ASSERT_TRUE(destination.tryPlace(std::move(destinationAmmo), {0, 0}));

    const QuantityTransferResult result =
        tryTransferItemQuantityFirstFit(
            source,
            destination,
            303,
            4,
            903);

    EXPECT_FALSE(result.succeeded);
    EXPECT_FALSE(result.consumedSplitInstanceId);
    EXPECT_EQ(source.placedItems().front().item.quantity(), 10U);
    EXPECT_EQ(destination.placedItems().front().item.quantity(), 58U);
}

TEST(InventoryStackTransferTest, WholeFirstFitTransferCanCompleteByMergeOnly)
{
    GridInventory source{{1, 1}};
    GridInventory destination{{1, 1}};
    ItemInstance sourceAmmo{304, ItemId::Ammo9mm, 5};
    ItemInstance destinationAmmo{404, ItemId::Ammo9mm, 55};
    ASSERT_TRUE(source.tryPlace(std::move(sourceAmmo), {0, 0}));
    ASSERT_TRUE(destination.tryPlace(std::move(destinationAmmo), {0, 0}));

    ASSERT_TRUE(tryTransferItemFirstFit(source, destination, 304));

    EXPECT_TRUE(source.placedItems().empty());
    ASSERT_EQ(destination.placedItems().size(), 1U);
    EXPECT_EQ(destination.placedItems().front().item.instanceId(), 404U);
    EXPECT_EQ(destination.placedItems().front().item.quantity(), 60U);
}

TEST(InventoryStackTransferTest, ConflictingSplitIdRejectsWithoutMutation)
{
    GridInventory source{{1, 1}};
    GridInventory destination{{1, 1}};
    ItemInstance sourceAmmo{305, ItemId::Ammo9mm, 10};
    ASSERT_TRUE(source.tryPlace(std::move(sourceAmmo), {0, 0}));

    const QuantityTransferResult result =
        tryTransferItemQuantityFirstFit(
            source,
            destination,
            305,
            4,
            305);

    EXPECT_FALSE(result.succeeded);
    EXPECT_FALSE(result.consumedSplitInstanceId);
    EXPECT_EQ(source.placedItems().front().item.quantity(), 10U);
    EXPECT_TRUE(destination.placedItems().empty());
}

TEST(InventoryQuantityPlacementTest, SplitsWithinSameInventoryAtExactCell)
{
    GridInventory inventory{{3, 1}};
    ItemInstance ammo{500, ItemId::Ammo9mm, 10};
    ASSERT_TRUE(inventory.tryPlace(std::move(ammo), {0, 0}));

    const QuantityTransferResult result =
        tryPlaceItemQuantityAt(
            inventory,
            inventory,
            500,
            1,
            {2, 0},
            ItemOrientation::Degrees0,
            900);

    EXPECT_EQ(result, (QuantityTransferResult{true, true}));
    EXPECT_EQ(inventory.quantityOf(500), 9U);
    EXPECT_EQ(inventory.quantityOf(900), 1U);
    EXPECT_EQ(inventory.originOf(900),
              (std::optional<GridPosition>{{2, 0}}));
}

TEST(InventoryQuantityPlacementTest, MergesWithinSameInventoryWithoutNewId)
{
    GridInventory inventory{{3, 1}};
    ItemInstance sourceAmmo{501, ItemId::Ammo9mm, 11};
    ItemInstance targetAmmo{601, ItemId::Ammo9mm, 50};
    ASSERT_TRUE(inventory.tryPlace(std::move(sourceAmmo), {0, 0}));
    ASSERT_TRUE(inventory.tryPlace(std::move(targetAmmo), {2, 0}));

    const QuantityTransferResult result =
        tryPlaceItemQuantityAt(
            inventory,
            inventory,
            501,
            5,
            {2, 0},
            ItemOrientation::Degrees0,
            901);

    EXPECT_EQ(result, (QuantityTransferResult{true, false}));
    EXPECT_EQ(inventory.quantityOf(501), 6U);
    EXPECT_EQ(inventory.quantityOf(601), 55U);
    EXPECT_EQ(inventory.originOf(901), std::nullopt);
}

TEST(InventoryQuantityPlacementTest, CrossContainerUsesExactEmptyCell)
{
    GridInventory source{{2, 1}};
    GridInventory destination{{3, 1}};
    ItemInstance ammo{502, ItemId::Ammo9mm, 9};
    ASSERT_TRUE(source.tryPlace(std::move(ammo), {0, 0}));

    const QuantityTransferResult result =
        tryPlaceItemQuantityAt(
            source,
            destination,
            502,
            4,
            {2, 0},
            ItemOrientation::Degrees0,
            902);

    EXPECT_EQ(result, (QuantityTransferResult{true, true}));
    EXPECT_EQ(source.quantityOf(502), 5U);
    EXPECT_EQ(destination.quantityOf(902), 4U);
    EXPECT_EQ(destination.originOf(902),
              (std::optional<GridPosition>{{2, 0}}));
    EXPECT_EQ(destination.occupantAt({0, 0}), std::nullopt);
}

TEST(InventoryQuantityPlacementTest, FullTargetRejectsWithoutMutation)
{
    GridInventory inventory{{2, 1}};
    ItemInstance sourceAmmo{503, ItemId::Ammo9mm, 10};
    ItemInstance targetAmmo{603, ItemId::Ammo9mm, 60};
    ASSERT_TRUE(inventory.tryPlace(std::move(sourceAmmo), {0, 0}));
    ASSERT_TRUE(inventory.tryPlace(std::move(targetAmmo), {1, 0}));
    const InventorySnapshot before = snapshot(inventory);

    EXPECT_FALSE(canPlaceItemQuantityAt(
        inventory,
        inventory,
        503,
        1,
        {1, 0},
        ItemOrientation::Degrees0));
    EXPECT_EQ(
        tryPlaceItemQuantityAt(
            inventory,
            inventory,
            503,
            1,
            {1, 0},
            ItemOrientation::Degrees0,
            903),
        (QuantityTransferResult{}));
    EXPECT_EQ(snapshot(inventory), before);
}
