#include <gtest/gtest.h>

#include <variant>

#include "inventory_interaction.h"

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

} // namespace

TEST(InventoryInteractionTest, StartsAsPureMouseIdleState)
{
    const InventoryInteractionState state;

    EXPECT_EQ(state.pointerPhase(), InventoryPointerPhase::Idle);
    EXPECT_EQ(state.selectedItem(), std::nullopt);
    EXPECT_EQ(state.hoveredLocation(), std::nullopt);
    EXPECT_EQ(state.activePreviewLocation(), std::nullopt);
    EXPECT_FALSE(state.pointerOverDropZone());
}

TEST(InventoryInteractionTest, ClickReleaseClearsTransientSelection)
{
    InventoryInteractionState state;

    ASSERT_TRUE(state.beginPointerPress(
        playerItem(11),
        {1, 1},
        {1, 1},
        {100.0F, 100.0F}));

    const auto request = state.releasePointer(
        {101.0F, 101.0F},
        InventoryGridLocation{
            InventoryContainerId::Player,
            {1, 1}},
        false);

    EXPECT_EQ(request, std::nullopt);
    EXPECT_EQ(state.selectedItem(), std::nullopt);
    EXPECT_EQ(state.pointerPhase(), InventoryPointerPhase::Idle);
}

TEST(InventoryInteractionTest, CrossContainerDragCreatesPlacementRequest)
{
    InventoryInteractionState state;

    ASSERT_TRUE(state.beginPointerPress(
        playerItem(12),
        {2, 1},
        {3, 2},
        {100.0F, 100.0F}));

    const auto request = state.releasePointer(
        {120.0F, 100.0F},
        InventoryGridLocation{
            InventoryContainerId::External,
            {4, 3}},
        false);

    ASSERT_TRUE(request.has_value());
    const auto *placement =
        std::get_if<InventoryPlacementRequest>(&*request);
    ASSERT_NE(placement, nullptr);
    EXPECT_EQ(placement->source, playerItem(12));
    EXPECT_EQ(
        placement->destination,
        (InventoryGridLocation{
            InventoryContainerId::External,
            {3, 2}}));
}

TEST(InventoryInteractionTest, PlayerDragToDropZoneCreatesDropRequest)
{
    InventoryInteractionState state;

    ASSERT_TRUE(state.beginPointerPress(
        playerItem(13),
        {0, 0},
        {0, 0},
        {20.0F, 20.0F}));

    const auto request = state.releasePointer(
        {40.0F, 40.0F},
        std::nullopt,
        true);

    ASSERT_TRUE(request.has_value());
    const auto *drop =
        std::get_if<InventoryDropRequest>(&*request);
    ASSERT_NE(drop, nullptr);
    EXPECT_EQ(drop->source, playerItem(13));
}

TEST(InventoryInteractionTest, ExternalItemCannotCreateDropRequest)
{
    InventoryInteractionState state;

    ASSERT_TRUE(state.beginPointerPress(
        externalItem(14),
        {0, 0},
        {0, 0},
        {20.0F, 20.0F}));

    EXPECT_EQ(
        state.releasePointer(
            {40.0F, 40.0F},
            std::nullopt,
            true),
        std::nullopt);
    EXPECT_EQ(state.selectedItem(), std::nullopt);
}

TEST(InventoryInteractionTest, OrdinaryOutsideReleaseCancels)
{
    InventoryInteractionState state;

    ASSERT_TRUE(state.beginPointerPress(
        playerItem(15),
        {0, 0},
        {0, 0},
        {10.0F, 10.0F}));

    EXPECT_EQ(
        state.releasePointer(
            {40.0F, 40.0F},
            std::nullopt,
            false),
        std::nullopt);
    EXPECT_EQ(state.pointerPhase(), InventoryPointerPhase::Idle);
}

TEST(InventoryInteractionTest, CancelClearsSelectionAndKeepsHover)
{
    InventoryInteractionState state;
    ASSERT_TRUE(state.beginPointerPress(
        playerItem(16),
        {0, 0},
        {0, 0},
        {10.0F, 10.0F}));

    const InventoryGridLocation hover{
        InventoryContainerId::External,
        {2, 2}};
    state.updatePointerPosition(
        {30.0F, 30.0F},
        hover,
        false);
    state.cancelPointerGesture();

    EXPECT_EQ(state.selectedItem(), std::nullopt);
    EXPECT_EQ(state.hoveredLocation(), hover);
    EXPECT_EQ(state.pointerPhase(), InventoryPointerPhase::Idle);
    EXPECT_EQ(state.activePreviewLocation(), std::nullopt);
}

TEST(InventoryOverlayStateTest, StartsClosed)
{
    const InventoryOverlayState state;

    EXPECT_FALSE(state.isOpen());
    EXPECT_FALSE(state.showsExternalContainer());
    EXPECT_EQ(state.mode(), InventoryOverlayMode::Closed);
}

TEST(InventoryOverlayStateTest, TabStyleOpenShowsOnlyPlayerInventory)
{
    InventoryOverlayState state;

    state.openPlayerInventory();

    EXPECT_TRUE(state.isOpen());
    EXPECT_FALSE(state.showsExternalContainer());
    EXPECT_EQ(state.mode(), InventoryOverlayMode::PlayerOnly);
}

TEST(InventoryOverlayStateTest, CabinetOpenShowsExternalContainer)
{
    InventoryOverlayState state;

    state.openContainerInventory();

    EXPECT_TRUE(state.isOpen());
    EXPECT_TRUE(state.showsExternalContainer());
    EXPECT_EQ(state.mode(), InventoryOverlayMode::Container);

    state.close();
    EXPECT_FALSE(state.isOpen());
    EXPECT_FALSE(state.showsExternalContainer());
}

TEST(InventoryContainerInteractionTest, InRangeInteractOpensAndConsumesGameplay)
{
    EXPECT_EQ(
        decideInventoryContainerInteraction(false, false, true, true),
        (InventoryContainerInteractionDecision{true, true}));
}

TEST(InventoryContainerInteractionTest, OutOfRangeInteractRemainsGameplayInput)
{
    EXPECT_EQ(
        decideInventoryContainerInteraction(false, false, false, true),
        (InventoryContainerInteractionDecision{false, false}));
}

TEST(InventoryContainerInteractionTest, OpenOverlaySuppressesWorldInput)
{
    EXPECT_EQ(
        decideInventoryContainerInteraction(true, false, true, true),
        (InventoryContainerInteractionDecision{false, true}));
}

TEST(InventoryContainerInteractionTest, TabOrEscControlWinsOverCabinetInteract)
{
    EXPECT_EQ(
        decideInventoryContainerInteraction(false, true, true, true),
        (InventoryContainerInteractionDecision{false, true}));
}

TEST(InventoryQuickTransferTest, ContainerIdleHoverCreatesRequest)
{
    const InventoryGridLocation hovered{
        InventoryContainerId::Player,
        {3, 2}};

    EXPECT_EQ(
        decideInventoryQuickTransfer(
            InventoryOverlayMode::Container,
            InventoryPointerPhase::Idle,
            hovered),
        (std::optional<InventoryQuickTransferRequest>{
            InventoryQuickTransferRequest{hovered}}));
}

TEST(InventoryPartialTransferTest, ModifierChoiceIsExclusive)
{
    EXPECT_EQ(
        decideInventoryPartialTransferMode(true, false),
        InventoryPartialTransferMode::One);
    EXPECT_EQ(
        decideInventoryPartialTransferMode(false, true),
        InventoryPartialTransferMode::Half);
    EXPECT_EQ(
        decideInventoryPartialTransferMode(false, false),
        std::nullopt);
    EXPECT_EQ(
        decideInventoryPartialTransferMode(true, true),
        std::nullopt);
}

TEST(InventoryPartialTransferTest, HalfRoundsOddQuantityUp)
{
    EXPECT_EQ(
        inventoryPartialTransferQuantity(
            InventoryPartialTransferMode::Half,
            5),
        3U);
    EXPECT_EQ(
        inventoryPartialTransferQuantity(
            InventoryPartialTransferMode::Half,
            6),
        3U);
    EXPECT_EQ(
        inventoryPartialTransferQuantity(
            InventoryPartialTransferMode::One,
            60),
        1U);
}

TEST(InventoryPartialTransferTest, QuantitySelectionStartsDraggingImmediately)
{
    InventoryInteractionState state;

    ASSERT_TRUE(state.beginQuantityPointerDrag(
        playerItem(71),
        {2, 1},
        {2, 1},
        {100.0F, 80.0F},
        InventoryPointerItemGeometry{
            ItemOrientation::Degrees0,
            {1, 1},
            false,
            {0.5F, 0.5F}},
        1));

    EXPECT_EQ(state.pointerPhase(), InventoryPointerPhase::Dragging);
    const auto visual = state.activeDragVisual();
    ASSERT_TRUE(visual.has_value());
    EXPECT_EQ(visual->selectedQuantity, 1U);
    EXPECT_EQ(
        state.activePreviewLocation(),
        (std::optional<InventoryGridLocation>{
            InventoryGridLocation{
                InventoryContainerId::Player,
                {2, 1}}}));
}

TEST(InventoryPartialTransferTest, QuantitySelectionIsCarriedByReleaseRequest)
{
    InventoryInteractionState state;
    ASSERT_TRUE(state.beginQuantityPointerDrag(
        playerItem(72),
        {0, 0},
        {0, 0},
        {10.0F, 10.0F},
        InventoryPointerItemGeometry{
            ItemOrientation::Degrees0,
            {1, 1},
            false,
            {0.25F, 0.25F}},
        5));

    const auto request = state.releasePointer(
        {30.0F, 10.0F},
        InventoryGridLocation{
            InventoryContainerId::Player,
            {3, 0}},
        false);

    ASSERT_TRUE(request.has_value());
    const auto *placement =
        std::get_if<InventoryPlacementRequest>(&*request);
    ASSERT_NE(placement, nullptr);
    EXPECT_EQ(placement->selectedQuantity, 5U);
    EXPECT_EQ(placement->destination.cell, (GridPosition{3, 0}));
}

TEST(InventoryPartialTransferTest, CancelClearsQuantitySelectionWithoutRequest)
{
    InventoryInteractionState state;
    ASSERT_TRUE(state.beginQuantityPointerDrag(
        playerItem(73),
        {0, 0},
        {0, 0},
        {10.0F, 10.0F},
        InventoryPointerItemGeometry{
            ItemOrientation::Degrees0,
            {1, 1},
            false,
            {0.5F, 0.5F}},
        3));

    state.cancelPointerGesture();

    EXPECT_EQ(state.pointerPhase(), InventoryPointerPhase::Idle);
    EXPECT_EQ(state.selectedItem(), std::nullopt);
    EXPECT_EQ(state.activeDragVisual(), std::nullopt);
}

TEST(InventoryPartialTransferTest, QuantitySelectionIsCarriedByDropRequest)
{
    InventoryInteractionState state;
    ASSERT_TRUE(state.beginQuantityPointerDrag(
        playerItem(74),
        {0, 0},
        {0, 0},
        {10.0F, 10.0F},
        InventoryPointerItemGeometry{
            ItemOrientation::Degrees0,
            {1, 1},
            false,
            {0.5F, 0.5F}},
        4));

    const auto request = state.releasePointer(
        {1200.0F, 100.0F},
        std::nullopt,
        true);

    ASSERT_TRUE(request.has_value());
    const auto *drop = std::get_if<InventoryDropRequest>(&*request);
    ASSERT_NE(drop, nullptr);
    EXPECT_EQ(drop->selectedQuantity, 4U);
}

TEST(InventoryQuickTransferTest, ExternalHoverPreservesSourceContainer)
{
    const InventoryGridLocation hovered{
        InventoryContainerId::External,
        {1, 4}};

    EXPECT_EQ(
        decideInventoryQuickTransfer(
            InventoryOverlayMode::Container,
            InventoryPointerPhase::Idle,
            hovered),
        (std::optional<InventoryQuickTransferRequest>{
            InventoryQuickTransferRequest{hovered}}));
}

TEST(InventoryQuickTransferTest, PlayerOnlyOverlayHasNoDestination)
{
    EXPECT_EQ(
        decideInventoryQuickTransfer(
            InventoryOverlayMode::PlayerOnly,
            InventoryPointerPhase::Idle,
            InventoryGridLocation{
                InventoryContainerId::Player,
                {0, 0}}),
        std::nullopt);
}

TEST(InventoryQuickTransferTest, MissingHoverDoesNotCreateRequest)
{
    EXPECT_EQ(
        decideInventoryQuickTransfer(
            InventoryOverlayMode::Container,
            InventoryPointerPhase::Idle,
            std::nullopt),
        std::nullopt);
}

TEST(InventoryQuickTransferTest, PressedOrDraggingGestureBlocksRequest)
{
    const InventoryGridLocation hovered{
        InventoryContainerId::Player,
        {0, 0}};

    EXPECT_EQ(
        decideInventoryQuickTransfer(
            InventoryOverlayMode::Container,
            InventoryPointerPhase::Pressed,
            hovered),
        std::nullopt);

    EXPECT_EQ(
        decideInventoryQuickTransfer(
            InventoryOverlayMode::Container,
            InventoryPointerPhase::Dragging,
            hovered),
        std::nullopt);
}

TEST(InventoryInteractionTest, ResetClearsAllPointerState)
{
    InventoryInteractionState state;
    ASSERT_TRUE(state.beginPointerPress(
        playerItem(17),
        {0, 0},
        {0, 0},
        {10.0F, 10.0F}));
    state.updatePointerPosition(
        {30.0F, 30.0F},
        std::nullopt,
        true);

    state.reset();

    EXPECT_EQ(state.selectedItem(), std::nullopt);
    EXPECT_EQ(state.hoveredLocation(), std::nullopt);
    EXPECT_EQ(state.activePreviewLocation(), std::nullopt);
    EXPECT_EQ(state.pointerDragDelta(), std::nullopt);
    EXPECT_FALSE(state.pointerOverDropZone());
    EXPECT_FALSE(state.pointerGestureActive());
}

TEST(InventoryRotationInteractionTest, RotationOnlyAppliesWhileDragging)
{
    InventoryInteractionState state;
    ASSERT_TRUE(state.beginPointerPress(
        playerItem(61),
        {1, 1},
        {2, 1},
        {100.0F, 100.0F},
        InventoryPointerItemGeometry{
            ItemOrientation::Degrees0,
            {4, 2},
            true,
            {1.25F, 0.5F}}));

    EXPECT_FALSE(state.rotatePointerItemClockwise());
    state.updatePointerPosition(
        {110.0F, 100.0F},
        InventoryGridLocation{
            InventoryContainerId::Player,
            {3, 2}},
        false);
    EXPECT_TRUE(state.rotatePointerItemClockwise());
}

TEST(InventoryRotationInteractionTest, RotationKeepsGrabPointUnderPointer)
{
    InventoryInteractionState state;
    ASSERT_TRUE(state.beginPointerPress(
        playerItem(62),
        {1, 1},
        {3, 2},
        {100.0F, 100.0F},
        InventoryPointerItemGeometry{
            ItemOrientation::Degrees0,
            {4, 2},
            true,
            {2.25F, 1.5F}}));
    state.updatePointerPosition(
        {120.0F, 100.0F},
        InventoryGridLocation{
            InventoryContainerId::External,
            {5, 4}},
        false);

    ASSERT_TRUE(state.rotatePointerItemClockwise());

    EXPECT_EQ(
        state.activePreviewLocation(),
        (std::optional<InventoryGridLocation>{
            InventoryGridLocation{
                InventoryContainerId::External,
                {5, 2}}}));
    const auto visual = state.activeDragVisual();
    ASSERT_TRUE(visual.has_value());
    EXPECT_EQ(visual->orientation, ItemOrientation::Degrees90);
    EXPECT_EQ(visual->footprint, (InventoryFootprint{2, 4}));
    EXPECT_FLOAT_EQ(visual->grabOffsetInCells.x, 0.5F);
    EXPECT_FLOAT_EQ(visual->grabOffsetInCells.y, 2.25F);
}

TEST(InventoryRotationInteractionTest, FourRotationsRestoreOriginalGeometry)
{
    InventoryInteractionState state;
    ASSERT_TRUE(state.beginPointerPress(
        playerItem(63),
        {0, 0},
        {1, 0},
        {10.0F, 10.0F},
        InventoryPointerItemGeometry{
            ItemOrientation::Degrees0,
            {2, 1},
            true,
            {1.5F, 0.25F}}));
    state.updatePointerPosition(
        {20.0F, 10.0F},
        InventoryGridLocation{
            InventoryContainerId::Player,
            {4, 3}},
        false);

    ASSERT_TRUE(state.rotatePointerItemClockwise());
    ASSERT_TRUE(state.rotatePointerItemClockwise());
    ASSERT_TRUE(state.rotatePointerItemClockwise());
    ASSERT_TRUE(state.rotatePointerItemClockwise());

    const auto visual = state.activeDragVisual();
    ASSERT_TRUE(visual.has_value());
    EXPECT_EQ(visual->orientation, ItemOrientation::Degrees0);
    EXPECT_EQ(visual->footprint, (InventoryFootprint{2, 1}));
    EXPECT_FLOAT_EQ(visual->grabOffsetInCells.x, 1.5F);
    EXPECT_FLOAT_EQ(visual->grabOffsetInCells.y, 0.25F);
    EXPECT_EQ(
        state.activePreviewLocation(),
        (std::optional<InventoryGridLocation>{
            InventoryGridLocation{
                InventoryContainerId::Player,
                {3, 3}}}));
}

TEST(InventoryRotationInteractionTest, ReleaseCarriesCandidateOrientation)
{
    InventoryInteractionState state;
    ASSERT_TRUE(state.beginPointerPress(
        playerItem(64),
        {0, 0},
        {0, 0},
        {10.0F, 10.0F},
        InventoryPointerItemGeometry{
            ItemOrientation::Degrees0,
            {2, 1},
            true,
            {0.25F, 0.25F}}));
    state.updatePointerPosition(
        {20.0F, 10.0F},
        InventoryGridLocation{
            InventoryContainerId::External,
            {2, 2}},
        false);
    ASSERT_TRUE(state.rotatePointerItemClockwise());

    const auto request = state.releasePointer(
        {20.0F, 10.0F},
        InventoryGridLocation{
            InventoryContainerId::External,
            {2, 2}},
        false);

    ASSERT_TRUE(request.has_value());
    const auto *placement =
        std::get_if<InventoryPlacementRequest>(&*request);
    ASSERT_NE(placement, nullptr);
    EXPECT_EQ(
        placement->orientation,
        ItemOrientation::Degrees90);
}

TEST(InventoryRotationInteractionTest, NonRotatableItemIgnoresRotation)
{
    InventoryInteractionState state;
    ASSERT_TRUE(state.beginPointerPress(
        playerItem(65),
        {0, 0},
        {0, 0},
        {10.0F, 10.0F},
        InventoryPointerItemGeometry{
            ItemOrientation::Degrees0,
            {2, 2},
            false,
            {0.5F, 0.5F}}));
    state.updatePointerPosition(
        {20.0F, 10.0F},
        InventoryGridLocation{
            InventoryContainerId::Player,
            {2, 2}},
        false);

    EXPECT_FALSE(state.rotatePointerItemClockwise());
    const auto visual = state.activeDragVisual();
    ASSERT_TRUE(visual.has_value());
    EXPECT_EQ(visual->orientation, ItemOrientation::Degrees0);
    EXPECT_EQ(visual->footprint, (InventoryFootprint{2, 2}));
}
