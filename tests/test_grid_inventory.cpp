#include <gtest/gtest.h>

#include <optional>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>

#include "grid_inventory.h"

namespace
{

    std::optional<ItemInstanceId> occupiedBy(
        ItemInstanceId instanceId)
    {
        return std::optional<ItemInstanceId>{
            instanceId};
    }
    std::vector<std::optional<ItemInstanceId>>
    snapshotCells(
        const GridInventory &inventory)
    {
        std::vector<
            std::optional<ItemInstanceId>>
            snapshot;

        snapshot.reserve(
            inventory.cellCount());

        for (
            int y = 0;
            y < inventory.height();
            ++y)
        {
            for (
                int x = 0;
                x < inventory.width();
                ++x)
            {
                snapshot.push_back(
                    inventory.occupantAt(
                        GridPosition{x, y}));
            }
        }

        return snapshot;
    }

} // namespace

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

    static_assert(
        std::is_nothrow_move_constructible_v<PlacedItem>);

    SUCCEED();
}

TEST(GridInventoryTest, OptionalItemInstanceIsMoveOnly)
{
    using OptionalItem =
        std::optional<ItemInstance>;

    static_assert(
        !std::is_copy_constructible_v<OptionalItem>);

    static_assert(
        std::is_move_constructible_v<OptionalItem>);

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

    EXPECT_TRUE(
        inventory.canPlace(
            ItemId::Medkit,
            GridPosition{8, 4}));

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

    EXPECT_FALSE(
        inventory.canPlace(
            ItemId::Rifle,
            GridPosition{7, 0}));
}

TEST(GridInventoryTest, RejectsBottomOverflow)
{
    GridInventory inventory({10, 6});

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
    EXPECT_EQ(
        result.value(),
        (GridPosition{0, 0}));
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
    EXPECT_EQ(
        first.value(),
        (GridPosition{0, 0}));
}

TEST(GridInventoryTest, FindFirstFitRejectsInvalidDefinition)
{
    GridInventory inventory({10, 6});

    EXPECT_THROW(
        inventory.findFirstFit(ItemId::Count),
        std::out_of_range);
}

TEST(GridInventoryTest, PlacesItemAndMarksAllCells)
{
    GridInventory inventory({10, 6});
    ItemInstance item{101, ItemId::Medkit};

    ASSERT_TRUE(
        inventory.tryPlace(
            std::move(item),
            GridPosition{2, 1}));

    ASSERT_EQ(
        inventory.placedItems().size(),
        1U);

    EXPECT_EQ(
        inventory.occupantAt({2, 1}),
        occupiedBy(101));
    EXPECT_EQ(
        inventory.occupantAt({3, 1}),
        occupiedBy(101));
    EXPECT_EQ(
        inventory.occupantAt({2, 2}),
        occupiedBy(101));
    EXPECT_EQ(
        inventory.occupantAt({3, 2}),
        occupiedBy(101));

    EXPECT_EQ(
        inventory.occupantAt({1, 1}),
        std::nullopt);
    EXPECT_EQ(
        inventory.occupantAt({4, 2}),
        std::nullopt);
}

TEST(GridInventoryTest, PreservesInstanceIdDefinitionAndOrigin)
{
    GridInventory inventory({10, 6});
    ItemInstance item{42, ItemId::Rifle};

    ASSERT_TRUE(
        inventory.tryPlace(
            std::move(item),
            GridPosition{3, 2}));

    const PlacedItem &placed =
        inventory.placedItems().front();

    EXPECT_EQ(placed.item.instanceId(), 42U);
    EXPECT_EQ(
        placed.item.definitionId(),
        ItemId::Rifle);
    EXPECT_EQ(
        placed.origin,
        (GridPosition{3, 2}));
}

TEST(GridInventoryTest, SuccessfulPlacementMovesInputItem)
{
    GridInventory inventory({10, 6});
    ItemInstance item{5, ItemId::Cola};

    ASSERT_TRUE(item.valid());

    ASSERT_TRUE(
        inventory.tryPlace(
            std::move(item),
            GridPosition{0, 0}));

    EXPECT_FALSE(item.valid());
    EXPECT_EQ(item.instanceId(), 0U);
    EXPECT_EQ(
        item.definitionId(),
        ItemId::Count);
}

TEST(GridInventoryTest, FailedPlacementDoesNotConsumeItem)
{
    GridInventory inventory({10, 6});
    ItemInstance item{6, ItemId::Rifle};

    EXPECT_FALSE(
        inventory.tryPlace(
            std::move(item),
            GridPosition{7, 0}));

    EXPECT_TRUE(item.valid());
    EXPECT_EQ(item.instanceId(), 6U);
    EXPECT_EQ(
        item.definitionId(),
        ItemId::Rifle);

    EXPECT_TRUE(
        inventory.placedItems().empty());
}

TEST(GridInventoryTest, FailedPlacementDoesNotChangeCells)
{
    GridInventory inventory({10, 6});
    ItemInstance item{7, ItemId::Rifle};

    EXPECT_FALSE(
        inventory.tryPlace(
            std::move(item),
            GridPosition{0, 5}));

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

TEST(GridInventoryTest, RejectsMovedFromItem)
{
    GridInventory inventory({10, 6});

    ItemInstance original{8, ItemId::Cola};
    ItemInstance owner{std::move(original)};

    ASSERT_FALSE(original.valid());
    ASSERT_TRUE(owner.valid());

    EXPECT_FALSE(
        inventory.tryPlace(
            std::move(original),
            GridPosition{0, 0}));

    EXPECT_TRUE(
        inventory.placedItems().empty());
    EXPECT_TRUE(owner.valid());
}

TEST(GridInventoryTest, RejectsDuplicateInstanceIdWithoutConsumingInput)
{
    GridInventory inventory({10, 6});

    ItemInstance first{9, ItemId::Cola};

    ASSERT_TRUE(
        inventory.tryPlace(
            std::move(first),
            GridPosition{0, 0}));

    ItemInstance duplicate{
        9,
        ItemId::Pistol};

    EXPECT_FALSE(
        inventory.tryPlace(
            std::move(duplicate),
            GridPosition{2, 0}));

    EXPECT_TRUE(duplicate.valid());
    EXPECT_EQ(duplicate.instanceId(), 9U);
    EXPECT_EQ(
        duplicate.definitionId(),
        ItemId::Pistol);

    EXPECT_EQ(
        inventory.placedItems().size(),
        1U);
    EXPECT_EQ(
        inventory.occupantAt({2, 0}),
        std::nullopt);
}

TEST(GridInventoryTest, RejectsOverlapAfterPlacement)
{
    GridInventory inventory({10, 6});

    ItemInstance medkit{
        10,
        ItemId::Medkit};

    ASSERT_TRUE(
        inventory.tryPlace(
            std::move(medkit),
            GridPosition{1, 1}));

    EXPECT_FALSE(
        inventory.canPlace(
            ItemId::Cola,
            GridPosition{1, 1}));

    EXPECT_FALSE(
        inventory.canPlace(
            ItemId::Pistol,
            GridPosition{0, 1}));

    ItemInstance overlappingCola{
        11,
        ItemId::Cola};

    EXPECT_FALSE(
        inventory.tryPlace(
            std::move(overlappingCola),
            GridPosition{2, 2}));

    EXPECT_TRUE(overlappingCola.valid());
}

TEST(GridInventoryTest, CanPlaceAdjacentItems)
{
    GridInventory inventory({10, 6});

    ItemInstance first{
        12,
        ItemId::Pistol};

    ASSERT_TRUE(
        inventory.tryPlace(
            std::move(first),
            GridPosition{0, 0}));

    EXPECT_TRUE(
        inventory.canPlace(
            ItemId::Pistol,
            GridPosition{2, 0}));

    ItemInstance second{
        13,
        ItemId::Pistol};

    ASSERT_TRUE(
        inventory.tryPlace(
            std::move(second),
            GridPosition{2, 0}));

    EXPECT_EQ(
        inventory.occupantAt({0, 0}),
        occupiedBy(12));
    EXPECT_EQ(
        inventory.occupantAt({1, 0}),
        occupiedBy(12));
    EXPECT_EQ(
        inventory.occupantAt({2, 0}),
        occupiedBy(13));
    EXPECT_EQ(
        inventory.occupantAt({3, 0}),
        occupiedBy(13));
}

TEST(GridInventoryTest, FindFirstFitSkipsOccupiedCells)
{
    GridInventory inventory({4, 2});

    ItemInstance pistol{
        14,
        ItemId::Pistol};

    ASSERT_TRUE(
        inventory.tryPlace(
            std::move(pistol),
            GridPosition{0, 0}));

    const auto fit =
        inventory.findFirstFit(ItemId::Cola);

    ASSERT_TRUE(fit.has_value());
    EXPECT_EQ(
        fit.value(),
        (GridPosition{2, 0}));
}

TEST(GridInventoryTest, FindFirstFitContinuesToNextRow)
{
    GridInventory inventory({4, 2});

    ItemInstance pistol{
        15,
        ItemId::Pistol};
    ItemInstance cola{
        16,
        ItemId::Cola};

    ASSERT_TRUE(
        inventory.tryPlace(
            std::move(pistol),
            GridPosition{0, 0}));

    ASSERT_TRUE(
        inventory.tryPlace(
            std::move(cola),
            GridPosition{2, 0}));

    const auto fit =
        inventory.findFirstFit(ItemId::Pistol);

    ASSERT_TRUE(fit.has_value());
    EXPECT_EQ(
        fit.value(),
        (GridPosition{0, 1}));
}

TEST(GridInventoryTest, FindFirstFitReturnsNulloptWhenNoSpace)
{
    GridInventory inventory({2, 1});

    ItemInstance pistol{
        17,
        ItemId::Pistol};

    ASSERT_TRUE(
        inventory.tryPlace(
            std::move(pistol),
            GridPosition{0, 0}));

    EXPECT_EQ(
        inventory.findFirstFit(ItemId::Cola),
        std::nullopt);
}

TEST(GridInventoryTest, RemoveReturnsOriginalItem)
{
    GridInventory inventory({10, 6});

    ItemInstance medkit{
        18,
        ItemId::Medkit};

    ASSERT_TRUE(
        inventory.tryPlace(
            std::move(medkit),
            GridPosition{3, 2}));

    std::optional<ItemInstance> removed =
        inventory.remove(18);

    ASSERT_TRUE(removed.has_value());
    EXPECT_TRUE(removed->valid());
    EXPECT_EQ(removed->instanceId(), 18U);
    EXPECT_EQ(
        removed->definitionId(),
        ItemId::Medkit);

    EXPECT_TRUE(
        inventory.placedItems().empty());
}

TEST(GridInventoryTest, RemoveClearsAllOccupiedCells)
{
    GridInventory inventory({10, 6});

    ItemInstance medkit{
        19,
        ItemId::Medkit};

    ASSERT_TRUE(
        inventory.tryPlace(
            std::move(medkit),
            GridPosition{3, 2}));

    ASSERT_TRUE(
        inventory.remove(19).has_value());

    EXPECT_EQ(
        inventory.occupantAt({3, 2}),
        std::nullopt);
    EXPECT_EQ(
        inventory.occupantAt({4, 2}),
        std::nullopt);
    EXPECT_EQ(
        inventory.occupantAt({3, 3}),
        std::nullopt);
    EXPECT_EQ(
        inventory.occupantAt({4, 3}),
        std::nullopt);

    EXPECT_TRUE(
        inventory.canPlace(
            ItemId::Medkit,
            GridPosition{3, 2}));
}

TEST(GridInventoryTest, RemoveMissingIdReturnsNullopt)
{
    GridInventory inventory({10, 6});

    ItemInstance cola{
        20,
        ItemId::Cola};

    ASSERT_TRUE(
        inventory.tryPlace(
            std::move(cola),
            GridPosition{0, 0}));

    EXPECT_EQ(
        inventory.remove(999),
        std::nullopt);

    ASSERT_EQ(
        inventory.placedItems().size(),
        1U);

    EXPECT_EQ(
        inventory.placedItems().front().item.instanceId(),
        20U);

    EXPECT_EQ(
        inventory.occupantAt({0, 0}),
        occupiedBy(20));
}

TEST(GridInventoryTest, RemoveKeepsRemainingItemsValid)
{
    GridInventory inventory({10, 6});

    ItemInstance pistol{
        21,
        ItemId::Pistol};
    ItemInstance medkit{
        22,
        ItemId::Medkit};

    ASSERT_TRUE(
        inventory.tryPlace(
            std::move(pistol),
            GridPosition{0, 0}));

    ASSERT_TRUE(
        inventory.tryPlace(
            std::move(medkit),
            GridPosition{4, 1}));

    ASSERT_TRUE(
        inventory.remove(21).has_value());

    ASSERT_EQ(
        inventory.placedItems().size(),
        1U);

    const PlacedItem &remaining =
        inventory.placedItems().front();

    EXPECT_TRUE(remaining.item.valid());
    EXPECT_EQ(
        remaining.item.instanceId(),
        22U);
    EXPECT_EQ(
        remaining.item.definitionId(),
        ItemId::Medkit);
    EXPECT_EQ(
        remaining.origin,
        (GridPosition{4, 1}));

    EXPECT_EQ(
        inventory.occupantAt({0, 0}),
        std::nullopt);
    EXPECT_EQ(
        inventory.occupantAt({1, 0}),
        std::nullopt);

    EXPECT_EQ(
        inventory.occupantAt({4, 1}),
        occupiedBy(22));
    EXPECT_EQ(
        inventory.occupantAt({5, 1}),
        occupiedBy(22));
    EXPECT_EQ(
        inventory.occupantAt({4, 2}),
        occupiedBy(22));
    EXPECT_EQ(
        inventory.occupantAt({5, 2}),
        occupiedBy(22));
}

TEST(GridInventoryTest, RemovedSpaceCanBeUsedAgain)
{
    GridInventory inventory({4, 2});

    ItemInstance rifle{
        23,
        ItemId::Rifle};

    ASSERT_TRUE(
        inventory.tryPlace(
            std::move(rifle),
            GridPosition{0, 0}));

    EXPECT_EQ(
        inventory.findFirstFit(ItemId::Cola),
        std::nullopt);

    ASSERT_TRUE(
        inventory.remove(23).has_value());

    const auto fit =
        inventory.findFirstFit(ItemId::Rifle);

    ASSERT_TRUE(fit.has_value());
    EXPECT_EQ(
        fit.value(),
        (GridPosition{0, 0}));
}

TEST(GridInventoryTest, CanMoveItemToEmptyArea)
{
    GridInventory inventory({10, 6});

    ItemInstance pistol{
        24,
        ItemId::Pistol};

    ASSERT_TRUE(
        inventory.tryPlace(
            std::move(pistol),
            GridPosition{0, 0}));

    EXPECT_TRUE(
        inventory.canMove(
            24,
            GridPosition{4, 3}));

    // canMove 是纯查询，不能修改原位置。
    ASSERT_EQ(
        inventory.placedItems().size(),
        1U);

    EXPECT_EQ(
        inventory.placedItems().front().origin,
        (GridPosition{0, 0}));

    EXPECT_EQ(
        inventory.occupantAt({0, 0}),
        occupiedBy(24));

    EXPECT_EQ(
        inventory.occupantAt({1, 0}),
        occupiedBy(24));

    EXPECT_EQ(
        inventory.occupantAt({4, 3}),
        std::nullopt);

    EXPECT_EQ(
        inventory.occupantAt({5, 3}),
        std::nullopt);
}

TEST(
    GridInventoryTest,
    CanMoveItemOverlappingItsOwnOldFootprint)
{
    GridInventory inventory({10, 6});

    ItemInstance medkit{
        25,
        ItemId::Medkit};

    ASSERT_TRUE(
        inventory.tryPlace(
            std::move(medkit),
            GridPosition{1, 1}));

    // Medkit 是 2×2。
    //
    // 原 footprint：
    // (1,1) (2,1)
    // (1,2) (2,2)
    //
    // 新 footprint：
    //       (2,1) (3,1)
    //       (2,2) (3,2)
    //
    // (2,1) 和 (2,2) 由同一个 instanceId 占用，
    // 因此应当允许。
    EXPECT_TRUE(
        inventory.canMove(
            25,
            GridPosition{2, 1}));

    // 查询后原 footprint 仍然存在。
    EXPECT_EQ(
        inventory.occupantAt({1, 1}),
        occupiedBy(25));

    EXPECT_EQ(
        inventory.occupantAt({2, 1}),
        occupiedBy(25));

    EXPECT_EQ(
        inventory.occupantAt({1, 2}),
        occupiedBy(25));

    EXPECT_EQ(
        inventory.occupantAt({2, 2}),
        occupiedBy(25));

    EXPECT_EQ(
        inventory.occupantAt({3, 1}),
        std::nullopt);

    EXPECT_EQ(
        inventory.occupantAt({3, 2}),
        std::nullopt);
}

TEST(GridInventoryTest, CanMoveToSameOrigin)
{
    GridInventory inventory({10, 6});

    ItemInstance rifle{
        26,
        ItemId::Rifle};

    ASSERT_TRUE(
        inventory.tryPlace(
            std::move(rifle),
            GridPosition{2, 2}));

    EXPECT_TRUE(
        inventory.canMove(
            26,
            GridPosition{2, 2}));

    ASSERT_EQ(
        inventory.placedItems().size(),
        1U);

    EXPECT_EQ(
        inventory.placedItems().front().origin,
        (GridPosition{2, 2}));

    EXPECT_EQ(
        inventory.occupantAt({2, 2}),
        occupiedBy(26));

    EXPECT_EQ(
        inventory.occupantAt({5, 3}),
        occupiedBy(26));
}

TEST(GridInventoryTest, RejectsMoveOutsideBounds)
{
    GridInventory inventory({10, 6});

    ItemInstance rifle{
        27,
        ItemId::Rifle};

    ASSERT_TRUE(
        inventory.tryPlace(
            std::move(rifle),
            GridPosition{0, 0}));

    EXPECT_FALSE(
        inventory.canMove(
            27,
            GridPosition{-1, 0}));

    EXPECT_FALSE(
        inventory.canMove(
            27,
            GridPosition{7, 0}));

    EXPECT_FALSE(
        inventory.canMove(
            27,
            GridPosition{0, 5}));

    // 失败查询不能修改原位置。
    EXPECT_EQ(
        inventory.placedItems().front().origin,
        (GridPosition{0, 0}));

    EXPECT_EQ(
        inventory.occupantAt({0, 0}),
        occupiedBy(27));

    EXPECT_EQ(
        inventory.occupantAt({3, 1}),
        occupiedBy(27));
}

TEST(GridInventoryTest, RejectsMoveOverAnotherItem)
{
    GridInventory inventory({10, 6});

    ItemInstance medkit{
        28,
        ItemId::Medkit};

    ItemInstance pistol{
        29,
        ItemId::Pistol};

    ASSERT_TRUE(
        inventory.tryPlace(
            std::move(medkit),
            GridPosition{0, 0}));

    ASSERT_TRUE(
        inventory.tryPlace(
            std::move(pistol),
            GridPosition{3, 0}));

    // Medkit 移动到 (2,0) 后会覆盖：
    //
    // (2,0) (3,0)
    // (2,1) (3,1)
    //
    // 其中 (3,0) 已由 Pistol 占用。
    EXPECT_FALSE(
        inventory.canMove(
            28,
            GridPosition{2, 0}));

    ASSERT_EQ(
        inventory.placedItems().size(),
        2U);

    EXPECT_EQ(
        inventory.placedItems()[0].origin,
        (GridPosition{0, 0}));

    EXPECT_EQ(
        inventory.placedItems()[1].origin,
        (GridPosition{3, 0}));

    EXPECT_EQ(
        inventory.occupantAt({0, 0}),
        occupiedBy(28));

    EXPECT_EQ(
        inventory.occupantAt({1, 1}),
        occupiedBy(28));

    EXPECT_EQ(
        inventory.occupantAt({3, 0}),
        occupiedBy(29));

    EXPECT_EQ(
        inventory.occupantAt({4, 0}),
        occupiedBy(29));
}

TEST(GridInventoryTest, RejectsMoveForMissingInstanceId)
{
    GridInventory inventory({10, 6});

    ItemInstance cola{
        30,
        ItemId::Cola};

    ASSERT_TRUE(
        inventory.tryPlace(
            std::move(cola),
            GridPosition{0, 0}));

    EXPECT_FALSE(
        inventory.canMove(
            999,
            GridPosition{1, 0}));

    ASSERT_EQ(
        inventory.placedItems().size(),
        1U);

    EXPECT_EQ(
        inventory.placedItems().front().item.instanceId(),
        30U);

    EXPECT_EQ(
        inventory.placedItems().front().origin,
        (GridPosition{0, 0}));

    EXPECT_EQ(
        inventory.occupantAt({0, 0}),
        occupiedBy(30));

    EXPECT_EQ(
        inventory.occupantAt({1, 0}),
        std::nullopt);
}

TEST(GridInventoryTest, SuccessfulMoveUpdatesOrigin)
{
    GridInventory inventory({10, 6});

    ItemInstance pistol{
        31,
        ItemId::Pistol};

    ASSERT_TRUE(
        inventory.tryPlace(
            std::move(pistol),
            GridPosition{0, 0}));

    ASSERT_TRUE(
        inventory.tryMove(
            31,
            GridPosition{4, 3}));

    ASSERT_EQ(
        inventory.placedItems().size(),
        1U);

    EXPECT_EQ(
        inventory.placedItems().front().origin,
        (GridPosition{4, 3}));
}

TEST(GridInventoryTest, SuccessfulMoveClearsOldCells)
{
    GridInventory inventory({10, 6});

    ItemInstance medkit{
        32,
        ItemId::Medkit};

    ASSERT_TRUE(
        inventory.tryPlace(
            std::move(medkit),
            GridPosition{1, 1}));

    ASSERT_TRUE(
        inventory.tryMove(
            32,
            GridPosition{5, 3}));

    EXPECT_EQ(
        inventory.occupantAt({1, 1}),
        std::nullopt);

    EXPECT_EQ(
        inventory.occupantAt({2, 1}),
        std::nullopt);

    EXPECT_EQ(
        inventory.occupantAt({1, 2}),
        std::nullopt);

    EXPECT_EQ(
        inventory.occupantAt({2, 2}),
        std::nullopt);
}

TEST(GridInventoryTest, SuccessfulMoveMarksNewCells)
{
    GridInventory inventory({10, 6});

    ItemInstance medkit{
        33,
        ItemId::Medkit};

    ASSERT_TRUE(
        inventory.tryPlace(
            std::move(medkit),
            GridPosition{0, 0}));

    ASSERT_TRUE(
        inventory.tryMove(
            33,
            GridPosition{4, 2}));

    EXPECT_EQ(
        inventory.occupantAt({4, 2}),
        occupiedBy(33));

    EXPECT_EQ(
        inventory.occupantAt({5, 2}),
        occupiedBy(33));

    EXPECT_EQ(
        inventory.occupantAt({4, 3}),
        occupiedBy(33));

    EXPECT_EQ(
        inventory.occupantAt({5, 3}),
        occupiedBy(33));
}

TEST(GridInventoryTest, SuccessfulMovePreservesInstanceId)
{
    GridInventory inventory({10, 6});

    ItemInstance rifle{
        34,
        ItemId::Rifle};

    ASSERT_TRUE(
        inventory.tryPlace(
            std::move(rifle),
            GridPosition{0, 0}));

    ASSERT_TRUE(
        inventory.tryMove(
            34,
            GridPosition{5, 2}));

    ASSERT_EQ(
        inventory.placedItems().size(),
        1U);

    const PlacedItem &placed =
        inventory.placedItems().front();

    EXPECT_TRUE(placed.item.valid());

    EXPECT_EQ(
        placed.item.instanceId(),
        34U);

    EXPECT_EQ(
        placed.item.definitionId(),
        ItemId::Rifle);

    EXPECT_EQ(
        placed.origin,
        (GridPosition{5, 2}));
}

TEST(
    GridInventoryTest,
    SuccessfulMoveHandlesSelfOverlap)
{
    GridInventory inventory({10, 6});

    ItemInstance medkit{
        35,
        ItemId::Medkit};

    ASSERT_TRUE(
        inventory.tryPlace(
            std::move(medkit),
            GridPosition{1, 1}));

    ASSERT_TRUE(
        inventory.tryMove(
            35,
            GridPosition{2, 1}));

    // 旧 footprint 中不再使用的左列被清空。
    EXPECT_EQ(
        inventory.occupantAt({1, 1}),
        std::nullopt);

    EXPECT_EQ(
        inventory.occupantAt({1, 2}),
        std::nullopt);

    // 新旧 footprint 重叠部分仍由原物品占用。
    EXPECT_EQ(
        inventory.occupantAt({2, 1}),
        occupiedBy(35));

    EXPECT_EQ(
        inventory.occupantAt({2, 2}),
        occupiedBy(35));

    // 新 footprint 新增的右列被正确写入。
    EXPECT_EQ(
        inventory.occupantAt({3, 1}),
        occupiedBy(35));

    EXPECT_EQ(
        inventory.occupantAt({3, 2}),
        occupiedBy(35));

    EXPECT_EQ(
        inventory.placedItems().front().origin,
        (GridPosition{2, 1}));
}

TEST(
    GridInventoryTest,
    FailedMoveLeavesOriginAndCellsUnchanged)
{
    GridInventory inventory({10, 6});

    ItemInstance medkit{
        36,
        ItemId::Medkit};

    ItemInstance pistol{
        37,
        ItemId::Pistol};

    ASSERT_TRUE(
        inventory.tryPlace(
            std::move(medkit),
            GridPosition{0, 0}));

    ASSERT_TRUE(
        inventory.tryPlace(
            std::move(pistol),
            GridPosition{3, 0}));

    const auto cellsBefore =
        snapshotCells(inventory);

    EXPECT_FALSE(
        inventory.tryMove(
            36,
            GridPosition{2, 0}));

    const auto cellsAfter =
        snapshotCells(inventory);

    EXPECT_EQ(
        cellsAfter,
        cellsBefore);

    ASSERT_EQ(
        inventory.placedItems().size(),
        2U);

    EXPECT_EQ(
        inventory.placedItems()[0].origin,
        (GridPosition{0, 0}));

    EXPECT_EQ(
        inventory.placedItems()[1].origin,
        (GridPosition{3, 0}));

    EXPECT_EQ(
        inventory.placedItems()[0]
            .item.instanceId(),
        36U);

    EXPECT_EQ(
        inventory.placedItems()[1]
            .item.instanceId(),
        37U);
}

TEST(
    GridInventoryTest,
    FailedMoveOutsideBoundsLeavesInventoryUnchanged)
{
    GridInventory inventory({10, 6});

    ItemInstance rifle{
        38,
        ItemId::Rifle};

    ASSERT_TRUE(
        inventory.tryPlace(
            std::move(rifle),
            GridPosition{2, 2}));

    const auto cellsBefore =
        snapshotCells(inventory);

    EXPECT_FALSE(
        inventory.tryMove(
            38,
            GridPosition{7, 2}));

    EXPECT_EQ(
        snapshotCells(inventory),
        cellsBefore);

    ASSERT_EQ(
        inventory.placedItems().size(),
        1U);

    EXPECT_EQ(
        inventory.placedItems().front().origin,
        (GridPosition{2, 2}));

    EXPECT_EQ(
        inventory.placedItems()
            .front()
            .item.instanceId(),
        38U);
}

TEST(GridInventoryTest, MissingIdMoveLeavesInventoryUnchanged)
{
    GridInventory inventory({10, 6});

    ItemInstance cola{
        39,
        ItemId::Cola};

    ASSERT_TRUE(
        inventory.tryPlace(
            std::move(cola),
            GridPosition{0, 0}));

    const auto cellsBefore =
        snapshotCells(inventory);

    EXPECT_FALSE(
        inventory.tryMove(
            999,
            GridPosition{1, 0}));

    EXPECT_EQ(
        snapshotCells(inventory),
        cellsBefore);

    ASSERT_EQ(
        inventory.placedItems().size(),
        1U);

    EXPECT_EQ(
        inventory.placedItems().front().origin,
        (GridPosition{0, 0}));

    EXPECT_EQ(
        inventory.placedItems()
            .front()
            .item.instanceId(),
        39U);
}

TEST(GridInventoryTest, SameOriginMoveIsNoOpSuccess)
{
    GridInventory inventory({10, 6});

    ItemInstance rifle{
        40,
        ItemId::Rifle};

    ASSERT_TRUE(
        inventory.tryPlace(
            std::move(rifle),
            GridPosition{2, 2}));

    const auto cellsBefore =
        snapshotCells(inventory);

    ASSERT_TRUE(
        inventory.tryMove(
            40,
            GridPosition{2, 2}));

    EXPECT_EQ(
        snapshotCells(inventory),
        cellsBefore);

    ASSERT_EQ(
        inventory.placedItems().size(),
        1U);

    EXPECT_EQ(
        inventory.placedItems().front().origin,
        (GridPosition{2, 2}));

    EXPECT_EQ(
        inventory.placedItems()
            .front()
            .item.instanceId(),
        40U);
}