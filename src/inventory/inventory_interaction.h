#pragma once

#include <optional>

#include "grid_inventory.h"

struct MousePosition
{
    int x{};
    int y{};
};

enum class InventoryInteractionState
{
    Idle,
    Selected,
    Dragging
};

class InventoryInteraction
{
public:
    InventoryInteraction(
        GridInventory &inventory,
        int inventoryX,
        int inventoryY,
        int cellSize);

    void updateMousePosition(
        MousePosition mouse);

    void pressMouse();

    void releaseMouse();

    [[nodiscard]]
    std::optional<GridPosition>
    focusedCell() const noexcept;

    [[nodiscard]]
    InventoryInteractionState
    state() const noexcept;

    [[nodiscard]]
    std::optional<ItemInstanceId>
    selectedInstanceId() const noexcept;

    [[nodiscard]]
    GridPosition previewOrigin() const noexcept;

private:
    [[nodiscard]]
    std::optional<GridPosition>
    screenToGrid(
        MousePosition mouse) const noexcept;

private:
    GridInventory &inventory_;

    int inventoryX_;
    int inventoryY_;

    int cellSize_;

    std::optional<GridPosition>
        focusedCell_;

    std::optional<ItemInstanceId>
        selectedInstanceId_;

    GridPosition previewOrigin_{};

    InventoryInteractionState state_{
        InventoryInteractionState::Idle};
};