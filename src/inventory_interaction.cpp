#include "inventory_interaction.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace
{
constexpr float inventoryDragThreshold = 4.0F;
}

InventoryFrameInputDecision decideInventoryFrameInput(
    bool inventoryOpen,
    bool toggleInventoryJustPressed,
    bool cancelJustPressed) noexcept
{
    if (toggleInventoryJustPressed)
    {
        return InventoryFrameInputDecision{
            inventoryOpen
                ? InventoryFrameControlAction::CloseInventory
                : InventoryFrameControlAction::OpenInventory,
            false,
            false};
    }

    if (inventoryOpen && cancelJustPressed)
    {
        return InventoryFrameInputDecision{
            InventoryFrameControlAction::CancelInteraction,
            false,
            false};
    }

    if (inventoryOpen)
    {
        return InventoryFrameInputDecision{
            InventoryFrameControlAction::None,
            true,
            true};
    }

    return InventoryFrameInputDecision{};
}

InventoryGridLayout::InventoryGridLayout(
    float gridX,
    float gridY,
    float cellSize,
    InventoryGridSize gridSize)
    : gridX_{gridX},
      gridY_{gridY},
      cellSize_{cellSize},
      gridSize_{gridSize}
{
    if (!std::isfinite(gridX_) ||
        !std::isfinite(gridY_))
    {
        throw std::invalid_argument(
            "Inventory grid origin must be finite");
    }

    if (!std::isfinite(cellSize_) ||
        cellSize_ <= 0.0F)
    {
        throw std::invalid_argument(
            "Inventory cell size must be finite and positive");
    }

    if (gridSize_.width <= 0 ||
        gridSize_.height <= 0)
    {
        throw std::invalid_argument(
            "Inventory grid dimensions must be positive");
    }

    const float gridWidth =
        cellSize_ *
        static_cast<float>(gridSize_.width);

    const float gridHeight =
        cellSize_ *
        static_cast<float>(gridSize_.height);

    if (!std::isfinite(gridWidth) ||
        !std::isfinite(gridHeight))
    {
        throw std::invalid_argument(
            "Inventory grid extent must be finite");
    }
}

float InventoryGridLayout::gridX() const noexcept
{
    return gridX_;
}

float InventoryGridLayout::gridY() const noexcept
{
    return gridY_;
}

float InventoryGridLayout::cellSize() const noexcept
{
    return cellSize_;
}

InventoryGridSize
InventoryGridLayout::gridSize() const noexcept
{
    return gridSize_;
}

std::optional<GridPosition>
InventoryGridLayout::screenToGrid(
    MousePosition position) const noexcept
{
    if (!std::isfinite(position.x) ||
        !std::isfinite(position.y))
    {
        return std::nullopt;
    }

    const float relativeX =
        position.x - gridX_;

    const float relativeY =
        position.y - gridY_;

    const float gridWidth =
        cellSize_ *
        static_cast<float>(gridSize_.width);

    const float gridHeight =
        cellSize_ *
        static_cast<float>(gridSize_.height);

    if (relativeX < 0.0F ||
        relativeY < 0.0F ||
        relativeX >= gridWidth ||
        relativeY >= gridHeight)
    {
        return std::nullopt;
    }

    return GridPosition{
        static_cast<int>(relativeX / cellSize_),
        static_cast<int>(relativeY / cellSize_)};
}

InventoryInteractionState::InventoryInteractionState(
    InventoryGridSize gridSize)
    : gridSize_{gridSize}
{
    if (gridSize_.width <= 0)
    {
        throw std::invalid_argument(
            "Inventory interaction width must be positive");
    }

    if (gridSize_.height <= 0)
    {
        throw std::invalid_argument(
            "Inventory interaction height must be positive");
    }
}

InventoryInteractionMode
InventoryInteractionState::mode() const noexcept
{
    return mode_;
}

GridPosition
InventoryInteractionState::focusedCell() const noexcept
{
    return focusedCell_;
}

std::optional<ItemInstanceId>
InventoryInteractionState::selectedInstanceId() const noexcept
{
    return selectedInstanceId_;
}

InventoryPointerPhase
InventoryInteractionState::pointerPhase() const noexcept
{
    return pointerPhase_;
}

std::optional<GridPosition>
InventoryInteractionState::hoveredCell() const noexcept
{
    return hoveredCell_;
}

std::optional<GridPosition>
InventoryInteractionState::activePreviewOrigin() const noexcept
{
    if (mode_ == InventoryInteractionMode::PlacingItem)
    {
        return previewOrigin_;
    }

    if (pointerPhase_ == InventoryPointerPhase::Dragging)
    {
        return pointerPreviewOrigin_;
    }

    return std::nullopt;
}

bool InventoryInteractionState::pointerGestureActive() const noexcept
{
    return pointerPhase_ != InventoryPointerPhase::Idle;
}

GridPosition
InventoryInteractionState::previewOrigin() const noexcept
{
    return previewOrigin_;
}

void InventoryInteractionState::moveFocus(
    int deltaX,
    int deltaY) noexcept
{
    if (
        mode_ !=
            InventoryInteractionMode::Browsing ||
        pointerGestureActive())
    {
        return;
    }

    focusedCell_ =
        clampToGrid(
            GridPosition{
                focusedCell_.x + deltaX,
                focusedCell_.y + deltaY});

    // Browsing 模式下没有正在移动的物品。
    // 让 previewOrigin 跟随焦点，避免残留旧预览坐标。
    previewOrigin_ =
        focusedCell_;
}

bool InventoryInteractionState::beginPlacement(
    std::optional<ItemInstanceId> instanceId,
    GridPosition itemOrigin) noexcept
{
    if (
        mode_ !=
            InventoryInteractionMode::Browsing ||
        pointerGestureActive())
    {
        return false;
    }

    if (
        !instanceId.has_value() ||
        *instanceId == 0)
    {
        return false;
    }

    clearPointerSelection();

    selectedInstanceId_ =
        instanceId;

    previewOrigin_ =
        clampToGrid(itemOrigin);

    mode_ =
        InventoryInteractionMode::PlacingItem;

    return true;
}

void InventoryInteractionState::movePreview(
    int deltaX,
    int deltaY) noexcept
{
    if (
        mode_ !=
        InventoryInteractionMode::PlacingItem)
    {
        return;
    }

    previewOrigin_ =
        clampToGrid(
            GridPosition{
                previewOrigin_.x + deltaX,
                previewOrigin_.y + deltaY});
}

void InventoryInteractionState::resolvePlacement(
    bool succeeded) noexcept
{
    if (
        mode_ !=
        InventoryInteractionMode::PlacingItem)
    {
        return;
    }

    // 非法确认不等于取消。
    // 保留选择和候选位置，让玩家继续调整。
    if (!succeeded)
    {
        return;
    }

    // 成功后浏览焦点移动到物品的新 origin。
    focusedCell_ =
        previewOrigin_;

    selectedInstanceId_.reset();

    mode_ =
        InventoryInteractionMode::Browsing;

    previewOrigin_ =
        focusedCell_;
}

void InventoryInteractionState::cancelPlacement() noexcept
{
    if (
        mode_ !=
        InventoryInteractionMode::PlacingItem)
    {
        return;
    }

    selectedInstanceId_.reset();

    mode_ =
        InventoryInteractionMode::Browsing;

    // 取消只清理 UI 预览状态。
    // 浏览焦点保持在开始放置前的位置。
    previewOrigin_ =
        focusedCell_;
}

void InventoryInteractionState::updatePointerPosition(
    MousePosition position,
    std::optional<GridPosition> gridCell) noexcept
{
    hoveredCell_ = gridCell;

    if (pointerPhase_ == InventoryPointerPhase::Idle ||
        !pointerPressPosition_.has_value())
    {
        return;
    }

    if (pointerPhase_ == InventoryPointerPhase::Pressed)
    {
        const float deltaX =
            position.x - pointerPressPosition_->x;

        const float deltaY =
            position.y - pointerPressPosition_->y;

        const float distanceSquared =
            deltaX * deltaX + deltaY * deltaY;

        const float thresholdSquared =
            inventoryDragThreshold * inventoryDragThreshold;

        if (distanceSquared >= thresholdSquared)
        {
            pointerPhase_ =
                InventoryPointerPhase::Dragging;
        }
    }

    if (pointerPhase_ != InventoryPointerPhase::Dragging)
    {
        return;
    }

    if (!gridCell.has_value())
    {
        pointerPreviewOrigin_.reset();
        return;
    }

    pointerPreviewOrigin_ =
        GridPosition{
            gridCell->x - grabOffset_.x,
            gridCell->y - grabOffset_.y};
}

bool InventoryInteractionState::beginPointerPress(
    std::optional<ItemInstanceId> instanceId,
    std::optional<GridPosition> itemOrigin,
    GridPosition clickedCell,
    MousePosition position) noexcept
{
    if (mode_ != InventoryInteractionMode::Browsing ||
        pointerGestureActive())
    {
        return false;
    }

    if (!instanceId.has_value() ||
        *instanceId == 0 ||
        !itemOrigin.has_value() ||
        clampToGrid(clickedCell) != clickedCell ||
        clampToGrid(*itemOrigin) != *itemOrigin ||
        !std::isfinite(position.x) ||
        !std::isfinite(position.y))
    {
        clearPointerSelection();
        return false;
    }

    selectedInstanceId_ = instanceId;
    pointerPressPosition_ = position;
    grabOffset_ =
        GridPosition{
            clickedCell.x - itemOrigin->x,
            clickedCell.y - itemOrigin->y};
    pointerPreviewOrigin_ = itemOrigin;
    pointerPhase_ = InventoryPointerPhase::Pressed;

    return true;
}

std::optional<InventoryMoveRequest>
InventoryInteractionState::releasePointer(
    MousePosition position,
    std::optional<GridPosition> gridCell) noexcept
{
    updatePointerPosition(position, gridCell);

    std::optional<InventoryMoveRequest> request;

    if (pointerPhase_ == InventoryPointerPhase::Dragging &&
        selectedInstanceId_.has_value() &&
        pointerPreviewOrigin_.has_value())
    {
        request = InventoryMoveRequest{
            *selectedInstanceId_,
            *pointerPreviewOrigin_};
    }

    resetPointerGesture();
    return request;
}

void InventoryInteractionState::cancelPointerGesture() noexcept
{
    resetPointerGesture();
}

void InventoryInteractionState::clearPointerSelection() noexcept
{
    if (mode_ == InventoryInteractionMode::Browsing &&
        !pointerGestureActive())
    {
        selectedInstanceId_.reset();
    }
}

void InventoryInteractionState::reset() noexcept
{
    mode_ = InventoryInteractionMode::Browsing;
    selectedInstanceId_.reset();
    hoveredCell_.reset();
    resetPointerGesture();
    previewOrigin_ = focusedCell_;
}

void InventoryInteractionState::resetPointerGesture() noexcept
{
    pointerPhase_ = InventoryPointerPhase::Idle;
    pointerPressPosition_.reset();
    pointerPreviewOrigin_.reset();
    grabOffset_ = GridPosition{0, 0};
}

GridPosition
InventoryInteractionState::clampToGrid(
    GridPosition position) const noexcept
{
    return GridPosition{
        std::clamp(
            position.x,
            0,
            gridSize_.width - 1),
        std::clamp(
            position.y,
            0,
            gridSize_.height - 1)};
}
