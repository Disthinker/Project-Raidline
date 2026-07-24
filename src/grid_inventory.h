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
    PlacedItem(
        ItemInstance &&itemValue,
        GridPosition originValue) noexcept;

    ~PlacedItem() = default;

    PlacedItem(const PlacedItem &) = delete;
    PlacedItem &operator=(const PlacedItem &) = delete;

    PlacedItem(PlacedItem &&) noexcept = default;
    PlacedItem &operator=(PlacedItem &&) noexcept = default;

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

    [[nodiscard]]
    bool canPlace(
        ItemId definitionId,
        GridPosition origin) const;

    [[nodiscard]]
    std::optional<GridPosition> findFirstFit(
        ItemId definitionId) const;

    // 判断已经放置的物品能否移动到 newOrigin。
    //
    // 检查目标 footprint 时：
    // - 空格允许；
    // - 由当前 instanceId 占用的格子允许；
    // - 由其他物品占用的格子拒绝。
    //
    // 该函数不修改 Inventory。
    [[nodiscard]]
    bool canMove(
        ItemInstanceId instanceId,
        GridPosition newOrigin) const;

    // 成功时把 item 的所有权转入背包。
    //
    // 失败时：
    // - cells_ 不变；
    // - placedItems_ 不变；
    // - 输入 item 不被移动。
    [[nodiscard]]
    bool tryPlace(
        ItemInstance &&item,
        GridPosition origin);

    // 找到物品时：
    // - 清除其全部占用格；
    // - 将原 ItemInstance 移出；
    // - 从 placedItems_ 删除记录。
    //
    // 找不到时返回 nullopt。
    [[nodiscard]]
    std::optional<ItemInstance> remove(
        ItemInstanceId instanceId);

    [[nodiscard]]
    std::optional<ItemInstanceId> occupantAt(
        GridPosition position) const noexcept;

    [[nodiscard]]
    const std::vector<PlacedItem> &placedItems() const noexcept;

private:
    [[nodiscard]]
    bool isWithinBounds(GridPosition position) const noexcept;

    // allowedOccupant 表示检查 footprint 时允许出现的占用者。
    //
    // 普通放置传入 nullopt：
    // 任何已占用格都拒绝。
    //
    // 移动检查传入当前物品的 instanceId：
    // 允许目标 footprint 与自己的旧 footprint 重叠。
    [[nodiscard]]
    bool canPlaceDefinition(
        const ItemDefinition &definition,
        GridPosition origin,
        std::optional<ItemInstanceId> allowedOccupant) const noexcept;

    [[nodiscard]]
    bool containsInstanceId(
        ItemInstanceId instanceId) const noexcept;

    // 将一个物品 footprint 中的所有格子统一设置为：
    // - instanceId：被物品占用；
    // - nullopt：清空占用。
    void setFootprintOccupant(
        const ItemDefinition &definition,
        GridPosition origin,
        std::optional<ItemInstanceId> occupant) noexcept;

    // 只能对已经确认合法的坐标调用。
    [[nodiscard]]
    std::size_t indexOf(GridPosition position) const noexcept;

    InventoryGridSize size_;
    std::vector<std::optional<ItemInstanceId>> cells_;
    std::vector<PlacedItem> placedItems_;
};