#include "inventory_interaction.h"

#include <cmath>
#include <stdexcept>

namespace
{
    constexpr float kInventoryDragThreshold{4.0F};

    bool isFinite(MousePosition position) noexcept
    {
        return std::isfinite(position.x) &&
               std::isfinite(position.y);
    }
}

bool InventoryScreenRect::contains(
    MousePosition position) const noexcept
{
    return isFinite(position) &&
           position.x >= x &&
           position.y >= y &&
           position.x < x + width &&
           position.y < y + height;
}

InventoryScreenRect makeRightEdgeInventoryDropZone(
    float screenWidth,
    float screenHeight,
    float zoneWidth)
{
    if (!std::isfinite(screenWidth) ||
        !std::isfinite(screenHeight) ||
        !std::isfinite(zoneWidth) ||
        screenWidth <= 0.0F ||
        screenHeight <= 0.0F ||
        zoneWidth <= 0.0F ||
        zoneWidth > screenWidth)
    {
        throw std::invalid_argument(
            "Inventory drop zone geometry must be finite and positive");
    }

    return InventoryScreenRect{
        screenWidth - zoneWidth,
        0.0F,
        zoneWidth,
        screenHeight};
}

InventoryOverlayMode InventoryOverlayState::mode() const noexcept
{
    return mode_;
}

bool InventoryOverlayState::isOpen() const noexcept
{
    return mode_ != InventoryOverlayMode::Closed;
}

bool InventoryOverlayState::showsExternalContainer() const noexcept
{
    return mode_ == InventoryOverlayMode::Container;
}

void InventoryOverlayState::openPlayerInventory() noexcept
{
    mode_ = InventoryOverlayMode::PlayerOnly;
}

void InventoryOverlayState::openContainerInventory() noexcept
{
    mode_ = InventoryOverlayMode::Container;
}

void InventoryOverlayState::close() noexcept
{
    mode_ = InventoryOverlayMode::Closed;
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
            false};
    }

    if (inventoryOpen && cancelJustPressed)
    {
        return InventoryFrameInputDecision{
            InventoryFrameControlAction::CancelInteraction,
            false};
    }

    if (inventoryOpen)
    {
        return InventoryFrameInputDecision{
            InventoryFrameControlAction::None,
            true};
    }

    return InventoryFrameInputDecision{};
}

InventoryContainerInteractionDecision
decideInventoryContainerInteraction(
    bool inventoryOpen,
    bool inventoryControlConsumedFrame,
    bool cabinetInRange,
    bool interactJustPressed) noexcept
{
    if (inventoryOpen || inventoryControlConsumedFrame)
    {
        return InventoryContainerInteractionDecision{
            false,
            true};
    }

    if (cabinetInRange && interactJustPressed)
    {
        return InventoryContainerInteractionDecision{
            true,
            true};
    }

    return InventoryContainerInteractionDecision{};
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
        cellSize_ * static_cast<float>(gridSize_.width);
    const float gridHeight =
        cellSize_ * static_cast<float>(gridSize_.height);

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
    if (!isFinite(position))
    {
        return std::nullopt;
    }

    const float relativeX = position.x - gridX_;
    const float relativeY = position.y - gridY_;
    const float gridWidth =
        cellSize_ * static_cast<float>(gridSize_.width);
    const float gridHeight =
        cellSize_ * static_cast<float>(gridSize_.height);

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

std::optional<InventoryItemSelection>
InventoryInteractionState::selectedItem() const noexcept
{
    return selectedItem_;
}

InventoryPointerPhase
InventoryInteractionState::pointerPhase() const noexcept
{
    return pointerPhase_;
}

std::optional<InventoryGridLocation>
InventoryInteractionState::hoveredLocation() const noexcept
{
    return hoveredLocation_;
}

std::optional<InventoryGridLocation>
InventoryInteractionState::activePreviewLocation() const noexcept
{
    if (pointerPhase_ != InventoryPointerPhase::Dragging)
    {
        return std::nullopt;
    }

    return pointerPreviewLocation_;
}

bool InventoryInteractionState::pointerOverDropZone() const noexcept
{
    return pointerOverDropZone_;
}

bool InventoryInteractionState::pointerGestureActive() const noexcept
{
    return pointerPhase_ != InventoryPointerPhase::Idle;
}

std::optional<MousePosition>
InventoryInteractionState::pointerDragDelta() const noexcept
{
    if (pointerPhase_ != InventoryPointerPhase::Dragging ||
        !pointerPressPosition_.has_value() ||
        !pointerCurrentPosition_.has_value())
    {
        return std::nullopt;
    }

    return MousePosition{
        pointerCurrentPosition_->x - pointerPressPosition_->x,
        pointerCurrentPosition_->y - pointerPressPosition_->y};
}

void InventoryInteractionState::updatePointerPosition(
    MousePosition position,
    std::optional<InventoryGridLocation> gridLocation,
    bool overDropZone) noexcept
{
    if (!isFinite(position))
    {
        hoveredLocation_.reset();
        pointerOverDropZone_ = false;

        if (pointerPhase_ == InventoryPointerPhase::Dragging)
        {
            pointerPreviewLocation_.reset();
        }

        return;
    }

    hoveredLocation_ = gridLocation;
    pointerOverDropZone_ =
        overDropZone && !gridLocation.has_value();

    if (pointerPhase_ == InventoryPointerPhase::Idle ||
        !pointerPressPosition_.has_value())
    {
        return;
    }

    pointerCurrentPosition_ = position;

    if (pointerPhase_ == InventoryPointerPhase::Pressed)
    {
        const float deltaX =
            position.x - pointerPressPosition_->x;
        const float deltaY =
            position.y - pointerPressPosition_->y;
        const float distanceSquared =
            deltaX * deltaX + deltaY * deltaY;
        const float thresholdSquared =
            kInventoryDragThreshold * kInventoryDragThreshold;

        if (distanceSquared >= thresholdSquared)
        {
            pointerPhase_ = InventoryPointerPhase::Dragging;
        }
    }

    if (pointerPhase_ != InventoryPointerPhase::Dragging)
    {
        return;
    }

    if (!gridLocation.has_value())
    {
        pointerPreviewLocation_.reset();
        return;
    }

    pointerPreviewLocation_ = InventoryGridLocation{
        gridLocation->container,
        GridPosition{
            gridLocation->cell.x - grabOffset_.x,
            gridLocation->cell.y - grabOffset_.y}};
}

bool InventoryInteractionState::beginPointerPress(
    InventoryItemSelection selection,
    GridPosition itemOrigin,
    GridPosition clickedCell,
    MousePosition position) noexcept
{
    if (pointerGestureActive())
    {
        return false;
    }

    if (selection.instanceId == 0 ||
        itemOrigin.x < 0 ||
        itemOrigin.y < 0 ||
        clickedCell.x < itemOrigin.x ||
        clickedCell.y < itemOrigin.y ||
        !isFinite(position))
    {
        clearSelection();
        return false;
    }

    selectedItem_ = selection;
    pointerPressPosition_ = position;
    pointerCurrentPosition_ = position;
    grabOffset_ = GridPosition{
        clickedCell.x - itemOrigin.x,
        clickedCell.y - itemOrigin.y};
    pointerPreviewLocation_ = InventoryGridLocation{
        selection.container,
        itemOrigin};
    pointerPhase_ = InventoryPointerPhase::Pressed;
    pointerOverDropZone_ = false;

    return true;
}

std::optional<InventoryPointerRequest>
InventoryInteractionState::releasePointer(
    MousePosition position,
    std::optional<InventoryGridLocation> gridLocation,
    bool overDropZone) noexcept
{
    updatePointerPosition(
        position,
        gridLocation,
        overDropZone);

    std::optional<InventoryPointerRequest> request;

    if (pointerPhase_ == InventoryPointerPhase::Dragging &&
        selectedItem_.has_value())
    {
        if (pointerPreviewLocation_.has_value())
        {
            request = InventoryPlacementRequest{
                *selectedItem_,
                *pointerPreviewLocation_};
        }
        else if (
            pointerOverDropZone_ &&
            selectedItem_->container ==
                InventoryContainerId::Player)
        {
            request = InventoryDropRequest{
                *selectedItem_};
        }
    }

    resetPointerGesture();
    selectedItem_.reset();
    return request;
}

void InventoryInteractionState::cancelPointerGesture() noexcept
{
    resetPointerGesture();
    selectedItem_.reset();
}

void InventoryInteractionState::clearSelection() noexcept
{
    if (!pointerGestureActive())
    {
        selectedItem_.reset();
    }
}

void InventoryInteractionState::reset() noexcept
{
    selectedItem_.reset();
    hoveredLocation_.reset();
    pointerOverDropZone_ = false;
    resetPointerGesture();
}

void InventoryInteractionState::resetPointerGesture() noexcept
{
    pointerPhase_ = InventoryPointerPhase::Idle;
    pointerPressPosition_.reset();
    pointerCurrentPosition_.reset();
    pointerPreviewLocation_.reset();
    grabOffset_ = GridPosition{0, 0};
}
