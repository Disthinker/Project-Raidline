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

std::optional<InventoryQuickTransferRequest>
decideInventoryQuickTransfer(
    InventoryOverlayMode overlayMode,
    InventoryPointerPhase pointerPhase,
    std::optional<InventoryGridLocation> hoveredLocation) noexcept
{
    if (overlayMode != InventoryOverlayMode::Container ||
        pointerPhase != InventoryPointerPhase::Idle ||
        !hoveredLocation.has_value())
    {
        return std::nullopt;
    }

    return InventoryQuickTransferRequest{
        *hoveredLocation};
}

std::optional<InventoryPartialTransferMode>
decideInventoryPartialTransferMode(
    bool controlPressed,
    bool shiftPressed) noexcept
{
    if (controlPressed == shiftPressed)
    {
        return std::nullopt;
    }

    return controlPressed
        ? InventoryPartialTransferMode::One
        : InventoryPartialTransferMode::Half;
}

std::uint32_t inventoryPartialTransferQuantity(
    InventoryPartialTransferMode mode,
    std::uint32_t availableQuantity) noexcept
{
    if (availableQuantity == 0)
    {
        return 0;
    }

    if (mode == InventoryPartialTransferMode::One)
    {
        return 1;
    }

    return availableQuantity / 2 +
           availableQuantity % 2;
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

InventoryPointerPhase InventoryDragGesture::phase() const noexcept
{
    return phase_;
}

bool InventoryDragGesture::active() const noexcept
{
    return phase_ != InventoryPointerPhase::Idle;
}

std::optional<MousePosition> InventoryDragGesture::dragDelta() const noexcept
{
    if (phase_ != InventoryPointerPhase::Dragging ||
        !pressPosition_.has_value() || !currentPosition_.has_value())
    {
        return std::nullopt;
    }
    return MousePosition{
        currentPosition_->x - pressPosition_->x,
        currentPosition_->y - pressPosition_->y};
}

std::optional<InventoryDragVisual> InventoryDragGesture::visual() const noexcept
{
    if (phase_ != InventoryPointerPhase::Dragging ||
        !currentPosition_.has_value())
    {
        return std::nullopt;
    }
    return InventoryDragVisual{
        *currentPosition_,
        geometry_.orientation,
        geometry_.footprint,
        geometry_.grabOffsetInCells,
        selectedQuantity_};
}

GridPosition InventoryDragGesture::grabOffset() const noexcept
{
    return grabOffset_;
}

bool InventoryDragGesture::begin(
    GridPosition itemOrigin,
    GridPosition clickedCell,
    MousePosition position,
    InventoryPointerItemGeometry geometry,
    std::optional<std::uint32_t> selectedQuantity) noexcept
{
    if (active() || itemOrigin.x < 0 || itemOrigin.y < 0 ||
        clickedCell.x < itemOrigin.x || clickedCell.y < itemOrigin.y ||
        !isFinite(position) ||
        (selectedQuantity.has_value() && *selectedQuantity == 0))
    {
        return false;
    }
    const GridPosition requestedGrabOffset{
        clickedCell.x - itemOrigin.x,
        clickedCell.y - itemOrigin.y};
    if (!isValidItemOrientation(geometry.orientation) ||
        geometry.footprint.width <= 0 || geometry.footprint.height <= 0 ||
        requestedGrabOffset.x >= geometry.footprint.width ||
        requestedGrabOffset.y >= geometry.footprint.height ||
        !isFinite(geometry.grabOffsetInCells) ||
        geometry.grabOffsetInCells.x < 0.0F ||
        geometry.grabOffsetInCells.y < 0.0F ||
        geometry.grabOffsetInCells.x >=
            static_cast<float>(geometry.footprint.width) ||
        geometry.grabOffsetInCells.y >=
            static_cast<float>(geometry.footprint.height) ||
        (!geometry.canRotate &&
         geometry.orientation != ItemOrientation::Degrees0))
    {
        return false;
    }
    pressPosition_ = position;
    currentPosition_ = position;
    grabOffset_ = requestedGrabOffset;
    geometry_ = geometry;
    selectedQuantity_ = selectedQuantity;
    phase_ = InventoryPointerPhase::Pressed;
    return true;
}

void InventoryDragGesture::update(MousePosition position) noexcept
{
    if (!active() || !isFinite(position) || !pressPosition_.has_value())
    {
        return;
    }
    currentPosition_ = position;
    if (phase_ == InventoryPointerPhase::Pressed)
    {
        const float deltaX = position.x - pressPosition_->x;
        const float deltaY = position.y - pressPosition_->y;
        const float thresholdSquared =
            kInventoryDragThreshold * kInventoryDragThreshold;
        if (deltaX * deltaX + deltaY * deltaY >= thresholdSquared)
        {
            phase_ = InventoryPointerPhase::Dragging;
        }
    }
}

bool InventoryDragGesture::rotateClockwise() noexcept
{
    if (phase_ != InventoryPointerPhase::Dragging || !geometry_.canRotate)
    {
        return false;
    }
    const InventoryFootprint oldFootprint = geometry_.footprint;
    const GridPosition oldGrabOffset = grabOffset_;
    const MousePosition oldGrabOffsetInCells = geometry_.grabOffsetInCells;
    geometry_.orientation = rotatedClockwise(geometry_.orientation);
    geometry_.footprint = InventoryFootprint{
        oldFootprint.height, oldFootprint.width};
    grabOffset_ = GridPosition{
        oldFootprint.height - 1 - oldGrabOffset.y,
        oldGrabOffset.x};
    geometry_.grabOffsetInCells = MousePosition{
        static_cast<float>(oldFootprint.height) - oldGrabOffsetInCells.y,
        oldGrabOffsetInCells.x};
    return true;
}

void InventoryDragGesture::reset() noexcept
{
    phase_ = InventoryPointerPhase::Idle;
    pressPosition_.reset();
    currentPosition_.reset();
    grabOffset_ = GridPosition{0, 0};
    geometry_ = InventoryPointerItemGeometry{};
    selectedQuantity_.reset();
}

std::optional<InventoryItemSelection>
InventoryInteractionState::selectedItem() const noexcept
{
    return selectedItem_;
}

InventoryPointerPhase
InventoryInteractionState::pointerPhase() const noexcept
{
    return gesture_.phase();
}

std::optional<InventoryGridLocation>
InventoryInteractionState::hoveredLocation() const noexcept
{
    return hoveredLocation_;
}

std::optional<InventoryGridLocation>
InventoryInteractionState::activePreviewLocation() const noexcept
{
    if (gesture_.phase() != InventoryPointerPhase::Dragging)
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
    return gesture_.active();
}

std::optional<MousePosition>
InventoryInteractionState::pointerDragDelta() const noexcept
{
    return gesture_.dragDelta();
}

std::optional<InventoryDragVisual>
InventoryInteractionState::activeDragVisual() const noexcept
{
    return gesture_.visual();
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

        if (gesture_.phase() == InventoryPointerPhase::Dragging)
        {
            pointerPreviewLocation_.reset();
        }

        return;
    }

    hoveredLocation_ = gridLocation;
    pointerOverDropZone_ =
        overDropZone && !gridLocation.has_value();

    if (!gesture_.active())
    {
        return;
    }

    gesture_.update(position);
    if (gesture_.phase() != InventoryPointerPhase::Dragging)
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
            gridLocation->cell.x - gesture_.grabOffset().x,
            gridLocation->cell.y - gesture_.grabOffset().y}};
}

bool InventoryInteractionState::beginPointerPress(
    InventoryItemSelection selection,
    GridPosition itemOrigin,
    GridPosition clickedCell,
    MousePosition position) noexcept
{
    const GridPosition grabOffset{
        clickedCell.x - itemOrigin.x,
        clickedCell.y - itemOrigin.y};

    return beginPointerPress(
        selection,
        itemOrigin,
        clickedCell,
        position,
        InventoryPointerItemGeometry{
            ItemOrientation::Degrees0,
            InventoryFootprint{
                grabOffset.x + 1,
                grabOffset.y + 1},
            false,
            MousePosition{
                static_cast<float>(grabOffset.x) + 0.5F,
                static_cast<float>(grabOffset.y) + 0.5F}});
}

bool InventoryInteractionState::beginPointerPress(
    InventoryItemSelection selection,
    GridPosition itemOrigin,
    GridPosition clickedCell,
    MousePosition position,
    InventoryPointerItemGeometry geometry) noexcept
{
    if (selection.instanceId == 0 ||
        !gesture_.begin(itemOrigin, clickedCell, position, geometry))
    {
        clearSelection();
        return false;
    }
    selectedItem_ = selection;
    pointerPreviewLocation_ = InventoryGridLocation{
        selection.container,
        itemOrigin};
    pointerOverDropZone_ = false;

    return true;
}

bool InventoryInteractionState::beginQuantityPointerDrag(
    InventoryItemSelection selection,
    GridPosition itemOrigin,
    GridPosition clickedCell,
    MousePosition position,
    InventoryPointerItemGeometry geometry,
    std::uint32_t selectedQuantity) noexcept
{
    if (selection.instanceId == 0 || selectedQuantity == 0 ||
        !gesture_.begin(
            itemOrigin,
            clickedCell,
            position,
            geometry,
            selectedQuantity))
    {
        clearSelection();
        return false;
    }
    selectedItem_ = selection;
    pointerPreviewLocation_ = InventoryGridLocation{
        selection.container,
        itemOrigin};
    pointerOverDropZone_ = false;
    return true;
}

bool InventoryInteractionState::rotatePointerItemClockwise() noexcept
{
    if (!gesture_.rotateClockwise())
    {
        return false;
    }

    if (hoveredLocation_.has_value())
    {
        pointerPreviewLocation_ = InventoryGridLocation{
            hoveredLocation_->container,
            GridPosition{
                hoveredLocation_->cell.x - gesture_.grabOffset().x,
                hoveredLocation_->cell.y - gesture_.grabOffset().y}};
    }
    else
    {
        pointerPreviewLocation_.reset();
    }

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

    const std::optional<InventoryDragVisual> visual = gesture_.visual();
    if (visual.has_value() &&
        selectedItem_.has_value())
    {
        if (pointerPreviewLocation_.has_value())
        {
            request = InventoryPlacementRequest{
                *selectedItem_,
                *pointerPreviewLocation_,
                visual->orientation,
                visual->selectedQuantity};
        }
        else if (
            pointerOverDropZone_ &&
            selectedItem_->container ==
                InventoryContainerId::Player)
        {
            request = InventoryDropRequest{
                *selectedItem_,
                visual->orientation,
                visual->selectedQuantity};
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
    gesture_.reset();
    pointerPreviewLocation_.reset();
}
