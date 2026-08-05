#include "inventory_interaction.h"

InventoryInteraction::InventoryInteraction(
    GridInventory &inventory,
    int inventoryX,
    int inventoryY,
    int cellSize)
    : inventory_(inventory),
      inventoryX_(inventoryX),
      inventoryY_(inventoryY),
      cellSize_(cellSize)
{
}

void InventoryInteraction::updateMousePosition(
    MousePosition mouse)
{
    auto position =
        screenToGrid(mouse);

    focusedCell_ =
        position;

    if (
        state_ ==
        InventoryInteractionState::Selected)
    {
        state_ =
            InventoryInteractionState::Dragging;
    }

    if (
        state_ ==
            InventoryInteractionState::Dragging &&
        position.has_value())
    {
        previewOrigin_ =
            position.value();
    }
}

void InventoryInteraction::pressMouse()
{
    if (!focusedCell_.has_value())
    {
        return;
    }

    auto occupant =
        inventory_.occupantAt(
            focusedCell_.value());

    if (!occupant.has_value())
    {
        return;
    }

    selectedInstanceId_ =
        occupant;

    previewOrigin_ =
        focusedCell_.value();

    state_ =
        InventoryInteractionState::Selected;
}

void InventoryInteraction::releaseMouse()
{
    if (
        state_ !=
        InventoryInteractionState::Dragging)
    {
        return;
    }

    bool moved = false;

    if (selectedInstanceId_.has_value())
    {
        moved =
            inventory_.tryMove(
                selectedInstanceId_.value(),
                previewOrigin_);
    }

    if (moved)
    {
        selectedInstanceId_.reset();

        state_ =
            InventoryInteractionState::Idle;
    }

    // 移动失败：
    // 保留 Selected/Dragging 状态
    // 等待玩家继续调整位置
}

std::optional<GridPosition>
InventoryInteraction::focusedCell() const noexcept
{
    return focusedCell_;
}

InventoryInteractionState
InventoryInteraction::state() const noexcept
{
    return state_;
}

std::optional<ItemInstanceId>
InventoryInteraction::selectedInstanceId() const noexcept
{
    return selectedInstanceId_;
}

GridPosition
InventoryInteraction::previewOrigin() const noexcept
{
    return previewOrigin_;
}

std::optional<GridPosition>
InventoryInteraction::screenToGrid(
    MousePosition mouse) const noexcept
{
    const int localX =
        mouse.x - inventoryX_;

    const int localY =
        mouse.y - inventoryY_;

    if (
        localX < 0 ||
        localY < 0)
    {
        return std::nullopt;
    }

    const int x =
        localX / cellSize_;

    const int y =
        localY / cellSize_;

    if (
        x < 0 ||
        y < 0 ||
        x >= inventory_.width() ||
        y >= inventory_.height())
    {
        return std::nullopt;
    }

    return GridPosition{
        x,
        y};
}