#pragma once

#include <cstddef>
#include <optional>
#include <vector>

#include "item_instance.h"

// 背包中的格子坐标，不是屏幕像素坐标。
struct GridPosition
{
    int x{};
    int y{};
};

// 背包的固定格子尺寸。
struct InventoryGridSize
{
    int width{};
    int height{};
};

// 已放入背包的物品。
// item 拥有真实 ItemInstance，origin 记录左上角格子。
struct PlacedItem
{
    ItemInstance item;
    GridPosition origin;
};

class GridInventory
{
public:
    explicit GridInventory(InventoryGridSize size);

    [[nodiscard]]
    int width() const noexcept;

    [[nodiscard]]
    int height() const noexcept;

    [[nodiscard]]
    std::size_t cellCount() const noexcept;

    // 返回指定格子的占用者 ID。
    //
    // nullopt 可能表示：
    // 1. 该坐标越界；
    // 2. 该格子合法但当前为空。
    [[nodiscard]]
    std::optional<ItemInstanceId> occupantAt(
        GridPosition position) const noexcept;

    // 仅提供只读访问，外部不能绕过 GridInventory 修改内容。
    [[nodiscard]]
    const std::vector<PlacedItem> &placedItems() const noexcept;

private:
    [[nodiscard]]
    bool isWithinBounds(GridPosition position) const noexcept;

    // 只能对已经确认合法的坐标调用。
    [[nodiscard]]
    std::size_t indexOf(GridPosition position) const noexcept;

    InventoryGridSize size_;
    std::vector<std::optional<ItemInstanceId>> cells_;
    std::vector<PlacedItem> placedItems_;
};