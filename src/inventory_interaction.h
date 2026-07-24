#pragma once

#include <optional>

#include "grid_inventory.h"

// 背包交互当前所处的逻辑模式。
//
// 该状态不依赖键盘、鼠标或 SDL。
// Enter、Esc、鼠标点击等输入由 App 翻译为成员函数调用。
enum class InventoryInteractionMode
{
    Browsing,
    PlacingItem
};

class InventoryInteractionState
{
public:
    explicit InventoryInteractionState(
        InventoryGridSize gridSize);

    [[nodiscard]]
    InventoryInteractionMode mode() const noexcept;

    // 键盘浏览焦点。
    // 未来鼠标 hoveredCell 不应与该字段混用。
    [[nodiscard]]
    GridPosition focusedCell() const noexcept;

    // 当前正在预览放置的物品稳定 ID。
    // Browsing 模式下为 nullopt。
    [[nodiscard]]
    std::optional<ItemInstanceId>
    selectedInstanceId() const noexcept;

    // PlacingItem 模式下表示候选左上角格子。
    [[nodiscard]]
    GridPosition previewOrigin() const noexcept;

    // 只在 Browsing 模式下移动 focusedCell。
    //
    // 结果被限制在背包格子范围内。
    void moveFocus(
        int deltaX,
        int deltaY) noexcept;

    // 从 Browsing 进入 PlacingItem。
    //
    // instanceId 为 nullopt 或 0 时失败。
    // itemOrigin 是物品当前 placement 的左上角。
    [[nodiscard]]
    bool beginPlacement(
        std::optional<ItemInstanceId> instanceId,
        GridPosition itemOrigin) noexcept;

    // 只在 PlacingItem 模式下移动预览左上角。
    //
    // 这里只限制左上角仍位于网格内。
    // 多格物品是否越界由 GridInventory::canMove 判断，
    // 从而允许 UI 显示红色非法预览。
    void movePreview(
        int deltaX,
        int deltaY) noexcept;

    // App 调用 GridInventory::tryMove 后，
    // 把结果交回状态机。
    //
    // false：保持 PlacingItem，继续调整。
    // true：回到 Browsing，并将焦点移动到新 origin。
    void resolvePlacement(
        bool succeeded) noexcept;

    // 取消当前放置。
    //
    // 原 Inventory 不需要回滚，因为预览期间没有修改它。
    void cancelPlacement() noexcept;

private:
    [[nodiscard]]
    GridPosition clampToGrid(
        GridPosition position) const noexcept;

    InventoryGridSize gridSize_;

    InventoryInteractionMode mode_{
        InventoryInteractionMode::Browsing};

    GridPosition focusedCell_{0, 0};

    std::optional<ItemInstanceId>
        selectedInstanceId_;

    GridPosition previewOrigin_{0, 0};
};