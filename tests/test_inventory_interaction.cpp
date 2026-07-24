#include <gtest/gtest.h>

#include <optional>
#include <stdexcept>

#include "inventory_interaction.h"

TEST(
    InventoryInteractionTest,
    StartsBrowsingAtTopLeft)
{
    InventoryInteractionState state({10, 6});

    EXPECT_EQ(
        state.mode(),
        InventoryInteractionMode::Browsing);

    EXPECT_EQ(
        state.focusedCell(),
        (GridPosition{0, 0}));

    EXPECT_EQ(
        state.previewOrigin(),
        (GridPosition{0, 0}));

    EXPECT_EQ(
        state.selectedInstanceId(),
        std::nullopt);
}

TEST(
    InventoryInteractionTest,
    RejectsInvalidGridDimensions)
{
    EXPECT_THROW(
        (InventoryInteractionState{
            InventoryGridSize{0, 6}}),
        std::invalid_argument);

    EXPECT_THROW(
        (InventoryInteractionState{
            InventoryGridSize{10, 0}}),
        std::invalid_argument);

    EXPECT_THROW(
        (InventoryInteractionState{
            InventoryGridSize{-1, 6}}),
        std::invalid_argument);
}

TEST(
    InventoryInteractionTest,
    BrowsingFocusMovesAndClamps)
{
    InventoryInteractionState state({10, 6});

    state.moveFocus(3, 2);

    EXPECT_EQ(
        state.focusedCell(),
        (GridPosition{3, 2}));

    state.moveFocus(-100, -100);

    EXPECT_EQ(
        state.focusedCell(),
        (GridPosition{0, 0}));

    state.moveFocus(100, 100);

    EXPECT_EQ(
        state.focusedCell(),
        (GridPosition{9, 5}));
}

TEST(
    InventoryInteractionTest,
    EmptySelectionDoesNotStartPlacement)
{
    InventoryInteractionState state({10, 6});

    EXPECT_FALSE(
        state.beginPlacement(
            std::nullopt,
            GridPosition{3, 2}));

    EXPECT_EQ(
        state.mode(),
        InventoryInteractionMode::Browsing);

    EXPECT_EQ(
        state.selectedInstanceId(),
        std::nullopt);

    EXPECT_EQ(
        state.focusedCell(),
        (GridPosition{0, 0}));
}

TEST(
    InventoryInteractionTest,
    InvalidZeroIdDoesNotStartPlacement)
{
    InventoryInteractionState state({10, 6});

    EXPECT_FALSE(
        state.beginPlacement(
            std::optional<ItemInstanceId>{0},
            GridPosition{1, 1}));

    EXPECT_EQ(
        state.mode(),
        InventoryInteractionMode::Browsing);

    EXPECT_EQ(
        state.selectedInstanceId(),
        std::nullopt);
}

TEST(
    InventoryInteractionTest,
    OccupiedSelectionStartsPlacement)
{
    InventoryInteractionState state({10, 6});

    state.moveFocus(3, 2);

    ASSERT_TRUE(
        state.beginPlacement(
            std::optional<ItemInstanceId>{42},
            GridPosition{2, 1}));

    EXPECT_EQ(
        state.mode(),
        InventoryInteractionMode::PlacingItem);

    EXPECT_EQ(
        state.selectedInstanceId(),
        std::optional<ItemInstanceId>{42});

    // previewOrigin 使用物品左上角，
    // 不一定等于玩家选中的 footprint 格子。
    EXPECT_EQ(
        state.previewOrigin(),
        (GridPosition{2, 1}));

    EXPECT_EQ(
        state.focusedCell(),
        (GridPosition{3, 2}));
}

TEST(
    InventoryInteractionTest,
    FocusDoesNotMoveWhilePlacing)
{
    InventoryInteractionState state({10, 6});

    state.moveFocus(2, 2);

    ASSERT_TRUE(
        state.beginPlacement(
            std::optional<ItemInstanceId>{43},
            GridPosition{1, 1}));

    state.moveFocus(4, 3);

    EXPECT_EQ(
        state.focusedCell(),
        (GridPosition{2, 2}));

    EXPECT_EQ(
        state.previewOrigin(),
        (GridPosition{1, 1}));
}

TEST(
    InventoryInteractionTest,
    PreviewMovesAndClampsWhilePlacing)
{
    InventoryInteractionState state({10, 6});

    ASSERT_TRUE(
        state.beginPlacement(
            std::optional<ItemInstanceId>{44},
            GridPosition{2, 2}));

    state.movePreview(3, 1);

    EXPECT_EQ(
        state.previewOrigin(),
        (GridPosition{5, 3}));

    state.movePreview(-100, -100);

    EXPECT_EQ(
        state.previewOrigin(),
        (GridPosition{0, 0}));

    state.movePreview(100, 100);

    // 只 clamp 左上角。
    // 对多格物品而言，该位置可能由 canMove 判定为非法。
    EXPECT_EQ(
        state.previewOrigin(),
        (GridPosition{9, 5}));
}

TEST(
    InventoryInteractionTest,
    PreviewDoesNotMoveWhileBrowsing)
{
    InventoryInteractionState state({10, 6});

    state.movePreview(5, 4);

    EXPECT_EQ(
        state.previewOrigin(),
        (GridPosition{0, 0}));

    EXPECT_EQ(
        state.mode(),
        InventoryInteractionMode::Browsing);
}

TEST(
    InventoryInteractionTest,
    RejectedPlacementStaysPlacing)
{
    InventoryInteractionState state({10, 6});

    ASSERT_TRUE(
        state.beginPlacement(
            std::optional<ItemInstanceId>{45},
            GridPosition{1, 1}));

    state.movePreview(2, 1);

    state.resolvePlacement(false);

    EXPECT_EQ(
        state.mode(),
        InventoryInteractionMode::PlacingItem);

    EXPECT_EQ(
        state.selectedInstanceId(),
        std::optional<ItemInstanceId>{45});

    EXPECT_EQ(
        state.previewOrigin(),
        (GridPosition{3, 2}));
}

TEST(
    InventoryInteractionTest,
    SuccessfulPlacementReturnsToBrowsing)
{
    InventoryInteractionState state({10, 6});

    state.moveFocus(1, 1);

    ASSERT_TRUE(
        state.beginPlacement(
            std::optional<ItemInstanceId>{46},
            GridPosition{1, 1}));

    state.movePreview(3, 2);

    state.resolvePlacement(true);

    EXPECT_EQ(
        state.mode(),
        InventoryInteractionMode::Browsing);

    EXPECT_EQ(
        state.selectedInstanceId(),
        std::nullopt);

    EXPECT_EQ(
        state.focusedCell(),
        (GridPosition{4, 3}));

    EXPECT_EQ(
        state.previewOrigin(),
        (GridPosition{4, 3}));
}

TEST(
    InventoryInteractionTest,
    CancelPlacementReturnsToOriginalFocus)
{
    InventoryInteractionState state({10, 6});

    state.moveFocus(4, 2);

    ASSERT_TRUE(
        state.beginPlacement(
            std::optional<ItemInstanceId>{47},
            GridPosition{3, 1}));

    state.movePreview(2, 2);

    state.cancelPlacement();

    EXPECT_EQ(
        state.mode(),
        InventoryInteractionMode::Browsing);

    EXPECT_EQ(
        state.selectedInstanceId(),
        std::nullopt);

    EXPECT_EQ(
        state.focusedCell(),
        (GridPosition{4, 2}));

    EXPECT_EQ(
        state.previewOrigin(),
        (GridPosition{4, 2}));
}

TEST(
    InventoryInteractionTest,
    CannotBeginAnotherPlacementWhilePlacing)
{
    InventoryInteractionState state({10, 6});

    ASSERT_TRUE(
        state.beginPlacement(
            std::optional<ItemInstanceId>{48},
            GridPosition{1, 1}));

    EXPECT_FALSE(
        state.beginPlacement(
            std::optional<ItemInstanceId>{49},
            GridPosition{5, 4}));

    EXPECT_EQ(
        state.selectedInstanceId(),
        std::optional<ItemInstanceId>{48});

    EXPECT_EQ(
        state.previewOrigin(),
        (GridPosition{1, 1}));
}