#include <gtest/gtest.h>

#include <cmath>
#include <optional>
#include <utility>
#include <variant>

#include "grid_inventory.h"
#include "inventory_interaction.h"
#include "inventory_transfer.h"

namespace
{

    constexpr InventoryItemSelection playerItem(
        ItemInstanceId instanceId)
    {
        return {
            InventoryContainerId::Player,
            instanceId};
    }

    constexpr InventoryItemSelection externalItem(
        ItemInstanceId instanceId)
    {
        return {
            InventoryContainerId::External,
            instanceId};
    }

    InventoryPlacementRequest dragPlacement(
        InventoryInteractionState &state,
        InventoryItemSelection source,
        GridPosition sourceOrigin,
        GridPosition clickedCell,
        InventoryGridLocation destination)
    {
        EXPECT_TRUE(state.beginPointerPress(
            source,
            sourceOrigin,
            clickedCell,
            {100.0F, 100.0F}));

        const auto request = state.releasePointer(
            {120.0F, 100.0F},
            destination,
            false);

        EXPECT_TRUE(request.has_value());
        const auto *placement = request.has_value()
            ? std::get_if<InventoryPlacementRequest>(&*request)
            : nullptr;
        EXPECT_NE(placement, nullptr);

        return placement != nullptr
            ? *placement
            : InventoryPlacementRequest{};
    }

} // namespace

TEST(MouseInventoryLayoutTest, RejectsInvalidGeometry)
{
    EXPECT_THROW(
        (InventoryGridLayout{0.0F, 0.0F, 0.0F, {10, 6}}),
        std::invalid_argument);
    EXPECT_THROW(
        (InventoryGridLayout{0.0F, 0.0F, 64.0F, {0, 6}}),
        std::invalid_argument);
    EXPECT_THROW(
        (InventoryGridLayout{
            std::nanf(""), 0.0F, 64.0F, {10, 6}}),
        std::invalid_argument);
}

TEST(MouseInventoryLayoutTest, ConvertsBoundariesWithoutOffByOne)
{
    const InventoryGridLayout layout{
        100.0F,
        50.0F,
        64.0F,
        {6, 6}};

    EXPECT_EQ(
        layout.screenToGrid({100.0F, 50.0F}),
        (std::optional<GridPosition>{GridPosition{0, 0}}));
    EXPECT_EQ(
        layout.screenToGrid({483.99F, 433.99F}),
        (std::optional<GridPosition>{GridPosition{5, 5}}));
    EXPECT_EQ(layout.screenToGrid({484.0F, 100.0F}), std::nullopt);
    EXPECT_EQ(layout.screenToGrid({120.0F, 434.0F}), std::nullopt);
}

TEST(MouseInventoryLayoutTest, DropZoneIsFlushWithRightScreenEdge)
{
    const InventoryScreenRect zone =
        makeRightEdgeInventoryDropZone(
            1280.0F,
            720.0F,
            96.0F);

    EXPECT_EQ(
        zone,
        (InventoryScreenRect{
            1184.0F,
            0.0F,
            96.0F,
            720.0F}));
    EXPECT_TRUE(zone.contains({1184.0F, 0.0F}));
    EXPECT_TRUE(zone.contains({1279.99F, 719.99F}));
    EXPECT_FALSE(zone.contains({1183.99F, 100.0F}));
    EXPECT_FALSE(zone.contains({1280.0F, 100.0F}));
}

TEST(MouseInventoryLayoutTest, DropZoneRejectsInvalidGeometry)
{
    EXPECT_THROW(
        makeRightEdgeInventoryDropZone(
            1280.0F,
            720.0F,
            0.0F),
        std::invalid_argument);
    EXPECT_THROW(
        makeRightEdgeInventoryDropZone(
            1280.0F,
            720.0F,
            1281.0F),
        std::invalid_argument);
}

TEST(InventoryFrameInputArbitrationTest, TabWinsAndSuppressesPointer)
{
    EXPECT_EQ(
        decideInventoryFrameInput(true, true, true),
        (InventoryFrameInputDecision{
            InventoryFrameControlAction::CloseInventory,
            false}));
}

TEST(InventoryFrameInputArbitrationTest, EscapeSuppressesPointer)
{
    EXPECT_EQ(
        decideInventoryFrameInput(true, false, true),
        (InventoryFrameInputDecision{
            InventoryFrameControlAction::CancelInteraction,
            false}));
}

TEST(InventoryFrameInputArbitrationTest, OpenFrameProcessesPointer)
{
    EXPECT_EQ(
        decideInventoryFrameInput(true, false, false),
        (InventoryFrameInputDecision{
            InventoryFrameControlAction::None,
            true}));
}

TEST(MouseInventoryInteractionTest, DragBeginsAtFourLogicalPixels)
{
    InventoryInteractionState state;
    ASSERT_TRUE(state.beginPointerPress(
        playerItem(21),
        {0, 0},
        {0, 0},
        {10.0F, 10.0F}));

    state.updatePointerPosition(
        {13.99F, 10.0F},
        InventoryGridLocation{InventoryContainerId::Player, {0, 0}},
        false);
    EXPECT_EQ(state.pointerPhase(), InventoryPointerPhase::Pressed);

    state.updatePointerPosition(
        {14.0F, 10.0F},
        InventoryGridLocation{InventoryContainerId::Player, {0, 0}},
        false);
    EXPECT_EQ(state.pointerPhase(), InventoryPointerPhase::Dragging);
}

TEST(MouseInventoryInteractionTest, CrossGridPreviewPreservesGrabOffset)
{
    InventoryInteractionState state;
    ASSERT_TRUE(state.beginPointerPress(
        playerItem(22),
        {1, 1},
        {2, 2},
        {10.0F, 10.0F}));

    state.updatePointerPosition(
        {30.0F, 30.0F},
        InventoryGridLocation{InventoryContainerId::External, {4, 3}},
        false);

    EXPECT_EQ(
        state.activePreviewLocation(),
        (std::optional<InventoryGridLocation>{
            InventoryGridLocation{
                InventoryContainerId::External,
                {3, 2}}}));
}

TEST(MouseInventoryInteractionTest, DragDeltaTracksSubCellMotionAcrossBlankSpace)
{
    InventoryInteractionState state;
    ASSERT_TRUE(state.beginPointerPress(
        playerItem(23),
        {0, 0},
        {0, 0},
        {100.0F, 80.0F}));

    state.updatePointerPosition(
        {112.5F, 86.25F},
        std::nullopt,
        false);

    EXPECT_EQ(
        state.pointerDragDelta(),
        (std::optional<MousePosition>{MousePosition{12.5F, 6.25F}}));
    EXPECT_EQ(state.activePreviewLocation(), std::nullopt);
}

TEST(MouseInventoryInteractionTest, DropZoneStateHasNoSnappedPreview)
{
    InventoryInteractionState state;
    ASSERT_TRUE(state.beginPointerPress(
        playerItem(24),
        {0, 0},
        {0, 0},
        {10.0F, 10.0F}));

    state.updatePointerPosition(
        {30.0F, 30.0F},
        std::nullopt,
        true);

    EXPECT_TRUE(state.pointerOverDropZone());
    EXPECT_EQ(state.activePreviewLocation(), std::nullopt);
    EXPECT_TRUE(state.pointerDragDelta().has_value());
}

TEST(MouseInventoryInteractionTest, NonFiniteMotionDoesNotPolluteDelta)
{
    InventoryInteractionState state;
    ASSERT_TRUE(state.beginPointerPress(
        playerItem(25),
        {0, 0},
        {0, 0},
        {10.0F, 10.0F}));
    state.updatePointerPosition(
        {20.0F, 10.0F},
        InventoryGridLocation{InventoryContainerId::Player, {0, 0}},
        false);

    state.updatePointerPosition(
        {std::nanf(""), 10.0F},
        std::nullopt,
        true);

    EXPECT_EQ(
        state.pointerDragDelta(),
        (std::optional<MousePosition>{MousePosition{10.0F, 0.0F}}));
    EXPECT_FALSE(state.pointerOverDropZone());
    EXPECT_EQ(state.activePreviewLocation(), std::nullopt);
}

TEST(MouseInventoryIntegrationTest, SameContainerDragUsesMoveTransaction)
{
    GridInventory inventory{{10, 6}};
    ItemInstance medkit{31, ItemId::Medkit};
    ASSERT_TRUE(inventory.tryPlace(std::move(medkit), {0, 0}));

    InventoryInteractionState state;
    const InventoryPlacementRequest request = dragPlacement(
        state,
        playerItem(31),
        {0, 0},
        {1, 1},
        {InventoryContainerId::Player, {4, 3}});

    ASSERT_EQ(request.destination.cell, (GridPosition{3, 2}));
    ASSERT_TRUE(
        inventory.tryMove(
            request.source.instanceId,
            request.destination.cell));
    EXPECT_EQ(
        inventory.originOf(31),
        (std::optional<GridPosition>{GridPosition{3, 2}}));
}

TEST(MouseInventoryIntegrationTest, CrossContainerDragUsesTransferTransaction)
{
    GridInventory player{{10, 6}};
    GridInventory external{{6, 6}};
    ItemInstance rifle{32, ItemId::Rifle};
    ASSERT_TRUE(player.tryPlace(std::move(rifle), {0, 0}));

    InventoryInteractionState state;
    const InventoryPlacementRequest request = dragPlacement(
        state,
        playerItem(32),
        {0, 0},
        {2, 1},
        {InventoryContainerId::External, {3, 2}});

    ASSERT_TRUE(tryTransferItem(
        player,
        external,
        request.source.instanceId,
        request.destination.cell));
    EXPECT_TRUE(player.placedItems().empty());
    EXPECT_EQ(
        external.originOf(32),
        (std::optional<GridPosition>{GridPosition{1, 1}}));
}

TEST(MouseInventoryIntegrationTest, RotatedSameContainerDragCommitsTransform)
{
    GridInventory inventory{{10, 6}};
    ItemInstance pistol{34, ItemId::Pistol};
    ASSERT_TRUE(inventory.tryPlace(std::move(pistol), {0, 0}));

    InventoryInteractionState state;
    ASSERT_TRUE(state.beginPointerPress(
        playerItem(34),
        {0, 0},
        {1, 0},
        {100.0F, 100.0F},
        InventoryPointerItemGeometry{
            ItemOrientation::Degrees0,
            {2, 1},
            true,
            {1.25F, 0.5F}}));
    state.updatePointerPosition(
        {120.0F, 120.0F},
        InventoryGridLocation{
            InventoryContainerId::Player,
            {4, 3}},
        false);
    ASSERT_TRUE(state.rotatePointerItemClockwise());

    const auto pointerRequest = state.releasePointer(
        {120.0F, 120.0F},
        InventoryGridLocation{
            InventoryContainerId::Player,
            {4, 3}},
        false);
    ASSERT_TRUE(pointerRequest.has_value());
    const auto *placement =
        std::get_if<InventoryPlacementRequest>(&*pointerRequest);
    ASSERT_NE(placement, nullptr);
    EXPECT_EQ(placement->destination.cell, (GridPosition{4, 2}));
    EXPECT_EQ(placement->orientation, ItemOrientation::Degrees90);

    ASSERT_TRUE(inventory.tryTransform(
        placement->source.instanceId,
        placement->destination.cell,
        placement->orientation));
    EXPECT_EQ(
        inventory.originOf(34),
        (std::optional<GridPosition>{GridPosition{4, 2}}));
    ASSERT_EQ(inventory.placedItems().size(), 1U);
    EXPECT_EQ(
        inventory.placedItems().front().item.orientation(),
        ItemOrientation::Degrees90);
    EXPECT_EQ(inventory.occupantAt({4, 2}), 34U);
    EXPECT_EQ(inventory.occupantAt({4, 3}), 34U);
    EXPECT_EQ(inventory.occupantAt({5, 2}), std::nullopt);
}

TEST(MouseInventoryIntegrationTest, RotatedCrossContainerDragCommitsTransform)
{
    GridInventory player{{10, 6}};
    GridInventory external{{6, 6}};
    ItemInstance rifle{35, ItemId::Rifle};
    ASSERT_TRUE(player.tryPlace(std::move(rifle), {0, 0}));

    InventoryInteractionState state;
    ASSERT_TRUE(state.beginPointerPress(
        playerItem(35),
        {0, 0},
        {2, 1},
        {100.0F, 100.0F},
        InventoryPointerItemGeometry{
            ItemOrientation::Degrees0,
            {4, 2},
            true,
            {2.25F, 1.5F}}));
    state.updatePointerPosition(
        {120.0F, 120.0F},
        InventoryGridLocation{
            InventoryContainerId::External,
            {4, 4}},
        false);
    ASSERT_TRUE(state.rotatePointerItemClockwise());

    const auto pointerRequest = state.releasePointer(
        {120.0F, 120.0F},
        InventoryGridLocation{
            InventoryContainerId::External,
            {4, 4}},
        false);
    ASSERT_TRUE(pointerRequest.has_value());
    const auto *placement =
        std::get_if<InventoryPlacementRequest>(&*pointerRequest);
    ASSERT_NE(placement, nullptr);
    EXPECT_EQ(placement->destination.cell, (GridPosition{4, 2}));
    EXPECT_EQ(placement->orientation, ItemOrientation::Degrees90);

    ASSERT_TRUE(tryTransferItemTransform(
        player,
        external,
        placement->source.instanceId,
        placement->destination.cell,
        placement->orientation));
    EXPECT_TRUE(player.placedItems().empty());
    EXPECT_EQ(
        external.originOf(35),
        (std::optional<GridPosition>{GridPosition{4, 2}}));
    ASSERT_EQ(external.placedItems().size(), 1U);
    EXPECT_EQ(
        external.placedItems().front().item.orientation(),
        ItemOrientation::Degrees90);
    EXPECT_EQ(external.occupantAt({4, 2}), 35U);
    EXPECT_EQ(external.occupantAt({5, 5}), 35U);
    EXPECT_EQ(external.occupantAt({3, 2}), std::nullopt);
}

TEST(MouseInventoryIntegrationTest, InvalidCrossContainerDropIsNonMutating)
{
    GridInventory player{{10, 6}};
    GridInventory external{{6, 6}};
    ItemInstance rifle{33, ItemId::Rifle};
    ASSERT_TRUE(player.tryPlace(std::move(rifle), {0, 0}));

    InventoryInteractionState state;
    const InventoryPlacementRequest request = dragPlacement(
        state,
        playerItem(33),
        {0, 0},
        {0, 0},
        {InventoryContainerId::External, {5, 5}});

    EXPECT_FALSE(tryTransferItem(
        player,
        external,
        request.source.instanceId,
        request.destination.cell));
    EXPECT_EQ(
        player.originOf(33),
        (std::optional<GridPosition>{GridPosition{0, 0}}));
    EXPECT_TRUE(external.placedItems().empty());
}

TEST(MouseInventoryFrameArbitrationTest, EscapeAndReleaseDoNotCommit)
{
    GridInventory inventory{{10, 6}};
    ItemInstance item{41, ItemId::Pistol};
    ASSERT_TRUE(inventory.tryPlace(std::move(item), {0, 0}));

    InventoryInteractionState state;
    ASSERT_TRUE(state.beginPointerPress(
        playerItem(41),
        {0, 0},
        {0, 0},
        {10.0F, 10.0F}));
    state.updatePointerPosition(
        {30.0F, 30.0F},
        InventoryGridLocation{InventoryContainerId::Player, {3, 2}},
        false);

    const InventoryFrameInputDecision decision =
        decideInventoryFrameInput(true, false, true);
    ASSERT_FALSE(decision.processUiEvents);
    state.cancelPointerGesture();

    EXPECT_EQ(
        inventory.originOf(41),
        (std::optional<GridPosition>{GridPosition{0, 0}}));
}

TEST(MouseInventoryFrameArbitrationTest, TabAndReleaseDoNotCommit)
{
    GridInventory inventory{{10, 6}};
    ItemInstance item{42, ItemId::Pistol};
    ASSERT_TRUE(inventory.tryPlace(std::move(item), {0, 0}));

    InventoryInteractionState state;
    ASSERT_TRUE(state.beginPointerPress(
        playerItem(42),
        {0, 0},
        {0, 0},
        {10.0F, 10.0F}));

    const InventoryFrameInputDecision decision =
        decideInventoryFrameInput(true, true, false);
    ASSERT_FALSE(decision.processUiEvents);
    state.reset();

    EXPECT_EQ(
        inventory.originOf(42),
        (std::optional<GridPosition>{GridPosition{0, 0}}));
}
