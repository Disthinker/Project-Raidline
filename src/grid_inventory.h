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

    friend bool operator==(
        const GridPosition &,
        const GridPosition &) = default;
};

// 背包的固定格子尺寸。
struct InventoryGridSize
{
    int width{};
    int height{};

    friend bool operator==(
        const InventoryGridSize &,
        const InventoryGridSize &) = default;
};

// 已放入背包的物品。
// item 拥有真实 ItemInstance，origin 是物品左上角格子。
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

    // 检查指定物品是否能够以 origin 为左上角放置。
    //
    // 检查内容：
    // 1. origin 是否越界；
    // 2. 物品矩形是否完全位于背包内；
    // 3. 物品覆盖的所有格子是否为空。
    //
    // 非法 ItemId 会由 itemDefinition() 抛出 std::out_of_range。
    [[nodiscard]]
    bool canPlace(
        ItemId definitionId,
        GridPosition origin) const;

    // 按 row-major 顺序寻找第一个合法位置：
    // y 从上到下，x 从左到右。
    //
    // 找不到位置时返回 nullopt。
    [[nodiscard]]
    std::optional<GridPosition> findFirstFit(
        ItemId definitionId) const;

    // 返回指定格子的占用者 ID。
    //
    // nullopt 可能表示：
    // 1. 坐标越界；
    // 2. 格子合法但当前为空。
    [[nodiscard]]
    std::optional<ItemInstanceId> occupantAt(
        GridPosition position) const noexcept;

    [[nodiscard]]
    const std::vector<PlacedItem> &placedItems() const noexcept;

private:
    [[nodiscard]]
    bool isWithinBounds(GridPosition position) const noexcept;

    // 内部版本接收已经验证过的物品定义，
    // 避免 findFirstFit 每个位置都重复查询定义目录。
    [[nodiscard]]
    bool canPlaceDefinition(
        const ItemDefinition &definition,
        GridPosition origin) const noexcept;

    // 只能对已经确认合法的坐标调用。
    [[nodiscard]]
    std::size_t indexOf(GridPosition position) const noexcept;

    InventoryGridSize size_;
    std::vector<std::optional<ItemInstanceId>> cells_;
    std::vector<PlacedItem> placedItems_;
};