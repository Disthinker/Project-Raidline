#include <gtest/gtest.h>

#include <limits>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>

#include "inventory_interaction.h"

namespace
{
std::vector<std::optional<ItemInstanceId>> snapshotCells(
    const GridInventory &inventory)
{
    std::vector<std::optional<ItemInstanceId>> result;
    result.reserve(inventory.cellCount());

    for (int y = 0; y < inventory.height(); ++y)
    {
        for (int x = 0; x < inventory.width(); ++x)
        {
            result.push_back(
                inventory.occupantAt({x, y}));
        }
    }

    return result;
}
} // namespace

TEST(MouseInventoryLayoutTest, RejectsInvalidGeometry)
{
    EXPECT_THROW(
        (InventoryGridLayout{0.0F, 0.0F, 0.0F, {10, 6}}),
        std::invalid_argument);

    EXPECT_THROW(
        (InventoryGridLayout{0.0F, 0.0F, -1.0F, {10, 6}}),
        std::invalid_argument);

    EXPECT_THROW(
        (InventoryGridLayout{
            0.0F,
            0.0F,
            std::numeric_limits<float>::quiet_NaN(),
            {10, 6}}),
        std::invalid_argument);

    EXPECT_THROW(
        (InventoryGridLayout{0.0F, 0.0F, 64.0F, {0, 6}}),
        std::invalid_argument);

    EXPECT_THROW(
        (InventoryGridLayout{
            std::numeric_limits<float>::infinity(),
            0.0F,
            64.0F,
            {10, 6}}),
        std::invalid_argument);

    EXPECT_THROW(
        (InventoryGridLayout{
            0.0F,
            0.0F,
            std::numeric_limits<float>::max(),
            {2, 2}}),
        std::invalid_argument);
}

TEST(MouseInventoryLayoutTest, ConvertsBoundariesWithoutOffByOne)
{
    const InventoryGridLayout layout{
        100.0F,
        50.0F,
        64.0F,
        {10, 6}};

    EXPECT_EQ(
        layout.screenToGrid({100.0F, 50.0F}),
        (std::optional<GridPosition>{{0, 0}}));

    EXPECT_EQ(
        layout.screenToGrid({163.999F, 113.999F}),
        (std::optional<GridPosition>{{0, 0}}));

    EXPECT_EQ(
        layout.screenToGrid({164.0F, 114.0F}),
        (std::optional<GridPosition>{{1, 1}}));

    EXPECT_EQ(
        layout.screenToGrid({739.999F, 433.999F}),
        (std::optional<GridPosition>{{9, 5}}));

    EXPECT_EQ(layout.screenToGrid({740.0F, 50.0F}), std::nullopt);
    EXPECT_EQ(layout.screenToGrid({100.0F, 434.0F}), std::nullopt);
    EXPECT_EQ(layout.screenToGrid({99.999F, 50.0F}), std::nullopt);
    EXPECT_EQ(layout.screenToGrid({100.0F, 49.999F}), std::nullopt);
}

TEST(MouseInventoryLayoutTest, RejectsNonFinitePointerCoordinates)
{
    const InventoryGridLayout layout{0.0F, 0.0F, 64.0F, {10, 6}};

    EXPECT_EQ(
        layout.screenToGrid(
            {std::numeric_limits<float>::quiet_NaN(), 0.0F}),
        std::nullopt);
}

TEST(InventoryFrameInputArbitrationTest, TabWinsOverEscapeAndSuppressesOtherInput)
{
    EXPECT_EQ(
        decideInventoryFrameInput(true, true, true),
        (InventoryFrameInputDecision{
            InventoryFrameControlAction::CloseInventory,
            false,
            false}));
}

TEST(InventoryFrameInputArbitrationTest, EscapeSuppressesPendingPointerRelease)
{
    EXPECT_EQ(
        decideInventoryFrameInput(true, false, true),
        (InventoryFrameInputDecision{
            InventoryFrameControlAction::CancelInteraction,
            false,
            false}));
}

TEST(InventoryFrameInputArbitrationTest, NormalOpenFrameProcessesPointerBeforeKeyboard)
{
    EXPECT_EQ(
        decideInventoryFrameInput(true, false, false),
        (InventoryFrameInputDecision{
            InventoryFrameControlAction::None,
            true,
            true}));
}

TEST(InventoryFrameInputArbitrationTest, OpeningFrameDoesNotReplayClosedInventoryPointerInput)
{
    EXPECT_EQ(
        decideInventoryFrameInput(false, true, false),
        (InventoryFrameInputDecision{
            InventoryFrameControlAction::OpenInventory,
            false,
            false}));
}

TEST(MouseInventoryInteractionTest, HoverDoesNotMoveKeyboardFocus)
{
    InventoryInteractionState state({10, 6});
    state.moveFocus(3, 2);

    state.updatePointerPosition({400.0F, 300.0F}, GridPosition{7, 4});

    EXPECT_EQ(state.hoveredCell(), (std::optional<GridPosition>{{7, 4}}));
    EXPECT_EQ(state.focusedCell(), (GridPosition{3, 2}));
}

TEST(MouseInventoryInteractionTest, EmptyPressClearsPersistentSelection)
{
    InventoryInteractionState state({10, 6});

    ASSERT_TRUE(
        state.beginPointerPress(12, GridPosition{1, 1}, {1, 1}, {10.0F, 10.0F}));

    EXPECT_EQ(
        state.releasePointer({10.0F, 10.0F}, GridPosition{1, 1}),
        std::nullopt);

    EXPECT_EQ(state.selectedInstanceId(), std::optional<ItemInstanceId>{12});

    EXPECT_FALSE(
        state.beginPointerPress(
            std::nullopt,
            std::nullopt,
            {4, 4},
            {20.0F, 20.0F}));

    EXPECT_EQ(state.selectedInstanceId(), std::nullopt);
}

TEST(MouseInventoryInteractionTest, RejectsInvalidPressCoordinates)
{
    InventoryInteractionState state({10, 6});

    EXPECT_FALSE(
        state.beginPointerPress(
            120,
            GridPosition{0, 0},
            GridPosition{-1, 0},
            {10.0F, 10.0F}));

    EXPECT_FALSE(
        state.beginPointerPress(
            120,
            GridPosition{0, 0},
            GridPosition{0, 0},
            {std::numeric_limits<float>::quiet_NaN(), 10.0F}));

    EXPECT_EQ(state.pointerPhase(), InventoryPointerPhase::Idle);
    EXPECT_EQ(state.selectedInstanceId(), std::nullopt);
}

TEST(MouseInventoryInteractionTest, DragBeginsAtFourLogicalPixels)
{
    InventoryInteractionState state({10, 6});

    ASSERT_TRUE(
        state.beginPointerPress(13, GridPosition{1, 1}, {1, 1}, {10.0F, 10.0F}));

    state.updatePointerPosition({13.9F, 10.0F}, GridPosition{1, 1});
    EXPECT_EQ(state.pointerPhase(), InventoryPointerPhase::Pressed);
    EXPECT_EQ(state.pointerDragDelta(), std::nullopt);

    state.updatePointerPosition({14.0F, 10.0F}, GridPosition{1, 1});
    EXPECT_EQ(state.pointerPhase(), InventoryPointerPhase::Dragging);
    EXPECT_EQ(
        state.pointerDragDelta(),
        (std::optional<MousePosition>{{4.0F, 0.0F}}));
}

TEST(MouseInventoryInteractionTest, DragPreservesMultiCellGrabOffset)
{
    InventoryInteractionState state({10, 6});

    ASSERT_TRUE(
        state.beginPointerPress(
            14,
            GridPosition{2, 1},
            GridPosition{4, 2},
            {10.0F, 10.0F}));

    state.updatePointerPosition({20.0F, 10.0F}, GridPosition{7, 4});

    EXPECT_EQ(
        state.activePreviewOrigin(),
        (std::optional<GridPosition>{{5, 3}}));
}

TEST(MouseInventoryInteractionTest, DragDeltaTracksSubCellPointerMotion)
{
    InventoryInteractionState state({10, 6});

    ASSERT_TRUE(
        state.beginPointerPress(
            141,
            GridPosition{2, 1},
            GridPosition{3, 1},
            {110.25F, 90.75F}));

    state.updatePointerPosition(
        {114.25F, 90.75F},
        GridPosition{3, 1});

    EXPECT_EQ(
        state.pointerDragDelta(),
        (std::optional<MousePosition>{{4.0F, 0.0F}}));
    EXPECT_EQ(
        state.activePreviewOrigin(),
        (std::optional<GridPosition>{{2, 1}}));

    // 指针仍在同一格内，但像素虚像必须继续连续移动。
    state.updatePointerPosition(
        {118.5F, 93.25F},
        GridPosition{3, 1});

    EXPECT_EQ(
        state.pointerDragDelta(),
        (std::optional<MousePosition>{{8.25F, 2.5F}}));
    EXPECT_EQ(
        state.activePreviewOrigin(),
        (std::optional<GridPosition>{{2, 1}}));
}

TEST(MouseInventoryInteractionTest, OutsideDragKeepsPixelDeltaButClearsGridPreview)
{
    InventoryInteractionState state({10, 6});

    ASSERT_TRUE(
        state.beginPointerPress(
            142,
            GridPosition{2, 1},
            GridPosition{3, 1},
            {10.0F, 10.0F}));

    state.updatePointerPosition(
        {20.0F, 18.0F},
        GridPosition{5, 3});
    ASSERT_EQ(state.pointerPhase(), InventoryPointerPhase::Dragging);

    state.updatePointerPosition({37.0F, 41.0F}, std::nullopt);

    EXPECT_EQ(
        state.pointerDragDelta(),
        (std::optional<MousePosition>{{27.0F, 31.0F}}));
    EXPECT_EQ(state.activePreviewOrigin(), std::nullopt);
}

TEST(MouseInventoryInteractionTest, NonFiniteMotionDoesNotPolluteDragDelta)
{
    InventoryInteractionState state({10, 6});

    ASSERT_TRUE(
        state.beginPointerPress(
            143,
            GridPosition{1, 1},
            GridPosition{1, 1},
            {10.0F, 10.0F}));

    state.updatePointerPosition(
        {20.0F, 10.0F},
        GridPosition{2, 1});
    ASSERT_EQ(
        state.pointerDragDelta(),
        (std::optional<MousePosition>{{10.0F, 0.0F}}));

    state.updatePointerPosition(
        {std::numeric_limits<float>::quiet_NaN(), 30.0F},
        std::nullopt);

    EXPECT_EQ(
        state.pointerDragDelta(),
        (std::optional<MousePosition>{{10.0F, 0.0F}}));
    EXPECT_EQ(state.activePreviewOrigin(), std::nullopt);
}

TEST(MouseInventoryInteractionTest, CancelAndResetClearDragDelta)
{
    InventoryInteractionState state({10, 6});

    ASSERT_TRUE(
        state.beginPointerPress(
            144,
            GridPosition{1, 1},
            GridPosition{1, 1},
            {10.0F, 10.0F}));
    state.updatePointerPosition(
        {20.0F, 10.0F},
        GridPosition{2, 1});
    ASSERT_TRUE(state.pointerDragDelta().has_value());

    state.cancelPointerGesture();
    EXPECT_EQ(state.pointerDragDelta(), std::nullopt);

    ASSERT_TRUE(
        state.beginPointerPress(
            145,
            GridPosition{1, 1},
            GridPosition{1, 1},
            {30.0F, 30.0F}));
    state.updatePointerPosition(
        {40.0F, 30.0F},
        GridPosition{2, 1});
    ASSERT_TRUE(state.pointerDragDelta().has_value());

    state.reset();
    EXPECT_EQ(state.pointerDragDelta(), std::nullopt);
}

TEST(MouseInventoryInteractionTest, GrabOffsetCanProduceNegativeCandidate)
{
    InventoryInteractionState state({10, 6});

    ASSERT_TRUE(
        state.beginPointerPress(
            140,
            GridPosition{2, 1},
            GridPosition{4, 2},
            {10.0F, 10.0F}));

    state.updatePointerPosition({20.0F, 10.0F}, GridPosition{0, 0});

    EXPECT_EQ(
        state.activePreviewOrigin(),
        (std::optional<GridPosition>{{-2, -1}}));
}

TEST(MouseInventoryInteractionTest, LeavingGridClearsPreviewAndReentryRestoresIt)
{
    InventoryInteractionState state({10, 6});

    ASSERT_TRUE(
        state.beginPointerPress(15, GridPosition{2, 1}, {3, 1}, {10.0F, 10.0F}));

    state.updatePointerPosition({20.0F, 10.0F}, GridPosition{5, 3});
    ASSERT_TRUE(state.activePreviewOrigin().has_value());

    state.updatePointerPosition({30.0F, 10.0F}, std::nullopt);
    EXPECT_EQ(state.activePreviewOrigin(), std::nullopt);

    state.updatePointerPosition({40.0F, 10.0F}, GridPosition{6, 4});
    EXPECT_EQ(
        state.activePreviewOrigin(),
        (std::optional<GridPosition>{{5, 4}}));
}

TEST(MouseInventoryInteractionTest, ClickSelectsWithoutMoveRequest)
{
    InventoryInteractionState state({10, 6});

    ASSERT_TRUE(
        state.beginPointerPress(16, GridPosition{2, 2}, {2, 2}, {10.0F, 10.0F}));

    EXPECT_EQ(
        state.releasePointer({12.0F, 10.0F}, GridPosition{2, 2}),
        std::nullopt);

    EXPECT_EQ(state.pointerPhase(), InventoryPointerPhase::Idle);
    EXPECT_EQ(state.selectedInstanceId(), std::optional<ItemInstanceId>{16});
}

TEST(MouseInventoryInteractionTest, OutsideReleaseCancelsCommitRequest)
{
    InventoryInteractionState state({10, 6});

    ASSERT_TRUE(
        state.beginPointerPress(17, GridPosition{2, 2}, {2, 2}, {10.0F, 10.0F}));

    EXPECT_EQ(state.releasePointer({20.0F, 10.0F}, std::nullopt), std::nullopt);
    EXPECT_EQ(state.pointerPhase(), InventoryPointerPhase::Idle);
    EXPECT_EQ(state.pointerDragDelta(), std::nullopt);
}

TEST(MouseInventoryInteractionTest, ValidDragReleaseReturnsMoveRequest)
{
    InventoryInteractionState state({10, 6});

    ASSERT_TRUE(
        state.beginPointerPress(18, GridPosition{1, 1}, {2, 1}, {10.0F, 10.0F}));

    EXPECT_EQ(
        state.releasePointer({20.0F, 10.0F}, GridPosition{6, 4}),
        (std::optional<InventoryMoveRequest>{
            InventoryMoveRequest{18, {5, 4}}}));
    EXPECT_EQ(state.pointerDragDelta(), std::nullopt);
}

TEST(MouseInventoryInteractionTest, EscapeStyleCancelKeepsSelection)
{
    InventoryInteractionState state({10, 6});

    ASSERT_TRUE(
        state.beginPointerPress(19, GridPosition{1, 1}, {1, 1}, {10.0F, 10.0F}));
    state.updatePointerPosition({20.0F, 10.0F}, GridPosition{3, 3});

    state.cancelPointerGesture();

    EXPECT_EQ(state.pointerPhase(), InventoryPointerPhase::Idle);
    EXPECT_EQ(state.activePreviewOrigin(), std::nullopt);
    EXPECT_EQ(state.selectedInstanceId(), std::optional<ItemInstanceId>{19});
}

TEST(MouseInventoryInteractionTest, KeyboardAndPointerGesturesAreMutuallyExclusive)
{
    InventoryInteractionState state({10, 6});

    ASSERT_TRUE(
        state.beginPointerPress(20, GridPosition{1, 1}, {1, 1}, {10.0F, 10.0F}));

    state.moveFocus(3, 2);
    EXPECT_EQ(state.focusedCell(), (GridPosition{0, 0}));
    EXPECT_FALSE(state.beginPlacement(20, GridPosition{1, 1}));

    state.cancelPointerGesture();
    ASSERT_TRUE(state.beginPlacement(20, GridPosition{1, 1}));

    EXPECT_FALSE(
        state.beginPointerPress(20, GridPosition{1, 1}, {1, 1}, {10.0F, 10.0F}));
}

TEST(MouseInventoryInteractionTest, ResetClearsAllTransientState)
{
    InventoryInteractionState state({10, 6});
    state.moveFocus(3, 2);
    state.updatePointerPosition({20.0F, 20.0F}, GridPosition{4, 4});

    ASSERT_TRUE(
        state.beginPointerPress(21, GridPosition{4, 4}, {4, 4}, {20.0F, 20.0F}));

    state.reset();

    EXPECT_EQ(state.mode(), InventoryInteractionMode::Browsing);
    EXPECT_EQ(state.pointerPhase(), InventoryPointerPhase::Idle);
    EXPECT_EQ(state.selectedInstanceId(), std::nullopt);
    EXPECT_EQ(state.hoveredCell(), std::nullopt);
    EXPECT_EQ(state.focusedCell(), (GridPosition{3, 2}));
}

TEST(MouseInventoryIntegrationTest, MultiCellDragCommitsThroughInventoryTransaction)
{
    GridInventory inventory({10, 6});
    ItemInstance rifle{101, ItemId::Rifle};
    ASSERT_TRUE(inventory.tryPlace(std::move(rifle), {1, 1}));

    InventoryInteractionState state({10, 6});
    ASSERT_TRUE(
        state.beginPointerPress(
            101,
            inventory.originOf(101),
            {3, 2},
            {10.0F, 10.0F}));

    const auto request =
        state.releasePointer({20.0F, 10.0F}, GridPosition{7, 4});

    ASSERT_TRUE(request.has_value());
    ASSERT_TRUE(inventory.canMove(request->instanceId, request->destination));
    ASSERT_TRUE(inventory.tryMove(request->instanceId, request->destination));
    EXPECT_EQ(inventory.originOf(101), (std::optional<GridPosition>{{5, 3}}));
}

TEST(MouseInventoryIntegrationTest, InvalidDropLeavesInventoryUnchanged)
{
    GridInventory inventory({10, 6});
    ItemInstance medkit{102, ItemId::Medkit};
    ItemInstance blocker{103, ItemId::Cola};
    ASSERT_TRUE(inventory.tryPlace(std::move(medkit), {0, 0}));
    ASSERT_TRUE(inventory.tryPlace(std::move(blocker), {4, 2}));
    const auto before = snapshotCells(inventory);

    InventoryInteractionState state({10, 6});
    ASSERT_TRUE(
        state.beginPointerPress(102, inventory.originOf(102), {1, 1}, {10.0F, 10.0F}));

    const auto request =
        state.releasePointer({20.0F, 10.0F}, GridPosition{5, 3});

    ASSERT_TRUE(request.has_value());
    EXPECT_FALSE(inventory.canMove(request->instanceId, request->destination));
    EXPECT_FALSE(inventory.tryMove(request->instanceId, request->destination));
    EXPECT_EQ(snapshotCells(inventory), before);
    EXPECT_EQ(inventory.originOf(102), (std::optional<GridPosition>{{0, 0}}));
}

TEST(MouseInventoryIntegrationTest, SelfOverlappingDragIsAllowed)
{
    GridInventory inventory({10, 6});
    ItemInstance rifle{105, ItemId::Rifle};
    ASSERT_TRUE(inventory.tryPlace(std::move(rifle), {1, 1}));

    InventoryInteractionState state({10, 6});
    ASSERT_TRUE(
        state.beginPointerPress(105, inventory.originOf(105), {3, 1}, {10.0F, 10.0F}));

    const auto request =
        state.releasePointer({20.0F, 10.0F}, GridPosition{4, 1});

    ASSERT_TRUE(request.has_value());
    EXPECT_TRUE(inventory.canMove(request->instanceId, request->destination));
    EXPECT_TRUE(inventory.tryMove(request->instanceId, request->destination));
    EXPECT_EQ(inventory.originOf(105), (std::optional<GridPosition>{{2, 1}}));
}

TEST(MouseInventoryIntegrationTest, SameCellDragCommitsAsNoOp)
{
    GridInventory inventory({10, 6});
    ItemInstance cola{106, ItemId::Cola};
    ASSERT_TRUE(inventory.tryPlace(std::move(cola), {3, 2}));

    InventoryInteractionState state({10, 6});
    ASSERT_TRUE(
        state.beginPointerPress(106, inventory.originOf(106), {3, 2}, {10.0F, 10.0F}));

    const auto request =
        state.releasePointer({14.0F, 10.0F}, GridPosition{3, 2});

    ASSERT_TRUE(request.has_value());
    EXPECT_TRUE(inventory.tryMove(request->instanceId, request->destination));
    EXPECT_EQ(inventory.originOf(106), (std::optional<GridPosition>{{3, 2}}));
}

TEST(MouseInventoryIntegrationTest, OutsideDropLeavesInventoryUnchanged)
{
    GridInventory inventory({10, 6});
    ItemInstance medkit{104, ItemId::Medkit};
    ASSERT_TRUE(inventory.tryPlace(std::move(medkit), {2, 2}));

    InventoryInteractionState state({10, 6});
    ASSERT_TRUE(
        state.beginPointerPress(104, inventory.originOf(104), {2, 2}, {10.0F, 10.0F}));

    EXPECT_EQ(state.releasePointer({20.0F, 10.0F}, std::nullopt), std::nullopt);
    EXPECT_EQ(inventory.originOf(104), (std::optional<GridPosition>{{2, 2}}));
}

TEST(MouseInventoryFrameArbitrationTest, EscapeAndPendingReleaseLeaveInventoryUnchanged)
{
    GridInventory inventory({10, 6});
    ItemInstance medkit{201, ItemId::Medkit};
    ASSERT_TRUE(inventory.tryPlace(std::move(medkit), {1, 1}));
    const auto before = snapshotCells(inventory);

    InventoryInteractionState state({10, 6});
    ASSERT_TRUE(
        state.beginPointerPress(
            201,
            inventory.originOf(201),
            {1, 1},
            {10.0F, 10.0F}));
    state.updatePointerPosition({20.0F, 10.0F}, GridPosition{6, 4});
    ASSERT_EQ(state.pointerPhase(), InventoryPointerPhase::Dragging);

    const InventoryFrameInputDecision decision =
        decideInventoryFrameInput(true, false, true);

    ASSERT_EQ(
        decision.controlAction,
        InventoryFrameControlAction::CancelInteraction);
    ASSERT_FALSE(decision.processPointerEvents);
    state.cancelPointerGesture();

    EXPECT_EQ(snapshotCells(inventory), before);
    EXPECT_EQ(inventory.originOf(201), (std::optional<GridPosition>{{1, 1}}));
    EXPECT_EQ(state.pointerPhase(), InventoryPointerPhase::Idle);
}

TEST(MouseInventoryFrameArbitrationTest, TabAndPendingReleaseLeaveInventoryUnchanged)
{
    GridInventory inventory({10, 6});
    ItemInstance rifle{202, ItemId::Rifle};
    ASSERT_TRUE(inventory.tryPlace(std::move(rifle), {1, 1}));
    const auto before = snapshotCells(inventory);

    InventoryInteractionState state({10, 6});
    ASSERT_TRUE(
        state.beginPointerPress(
            202,
            inventory.originOf(202),
            {2, 1},
            {10.0F, 10.0F}));
    state.updatePointerPosition({20.0F, 10.0F}, GridPosition{7, 4});
    ASSERT_EQ(state.pointerPhase(), InventoryPointerPhase::Dragging);

    const InventoryFrameInputDecision decision =
        decideInventoryFrameInput(true, true, false);

    ASSERT_EQ(
        decision.controlAction,
        InventoryFrameControlAction::CloseInventory);
    ASSERT_FALSE(decision.processPointerEvents);
    state.reset();

    EXPECT_EQ(snapshotCells(inventory), before);
    EXPECT_EQ(inventory.originOf(202), (std::optional<GridPosition>{{1, 1}}));
    EXPECT_EQ(state.pointerPhase(), InventoryPointerPhase::Idle);
}

TEST(MouseInventoryFrameArbitrationTest, NormalPendingReleaseCommitsMove)
{
    GridInventory inventory({10, 6});
    ItemInstance cola{203, ItemId::Cola};
    ASSERT_TRUE(inventory.tryPlace(std::move(cola), {1, 1}));

    InventoryInteractionState state({10, 6});
    ASSERT_TRUE(
        state.beginPointerPress(
            203,
            inventory.originOf(203),
            {1, 1},
            {10.0F, 10.0F}));

    const InventoryFrameInputDecision decision =
        decideInventoryFrameInput(true, false, false);

    ASSERT_TRUE(decision.processPointerEvents);
    const auto request =
        state.releasePointer({20.0F, 10.0F}, GridPosition{5, 3});
    ASSERT_TRUE(request.has_value());
    ASSERT_TRUE(inventory.canMove(request->instanceId, request->destination));
    ASSERT_TRUE(inventory.tryMove(request->instanceId, request->destination));

    EXPECT_EQ(inventory.originOf(203), (std::optional<GridPosition>{{5, 3}}));
}

TEST(MouseInventoryFrameArbitrationTest, PointerPressSuppressesSameFrameKeyboardMove)
{
    InventoryInteractionState state({10, 6});
    const InventoryFrameInputDecision decision =
        decideInventoryFrameInput(true, false, false);

    ASSERT_TRUE(decision.processPointerEvents);
    ASSERT_TRUE(
        state.beginPointerPress(
            204,
            GridPosition{2, 2},
            {2, 2},
            {10.0F, 10.0F}));

    ASSERT_TRUE(decision.processKeyboardInput);
    state.moveFocus(1, 0);

    EXPECT_EQ(state.focusedCell(), (GridPosition{0, 0}));
    EXPECT_EQ(state.pointerPhase(), InventoryPointerPhase::Pressed);
}
