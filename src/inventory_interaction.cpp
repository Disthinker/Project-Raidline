#include "inventory_interaction.h"

#include <algorithm>
#include <stdexcept>

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
        InventoryInteractionMode::Browsing)
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
        InventoryInteractionMode::Browsing)
    {
        return false;
    }

    if (
        !instanceId.has_value() ||
        *instanceId == 0)
    {
        return false;
    }

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