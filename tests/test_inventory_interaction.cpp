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
