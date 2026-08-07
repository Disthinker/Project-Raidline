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

    GridInventory(const GridInventory &) = delete;
    GridInventory &operator=(const GridInventory &) = delete;

    GridInventory(GridInventory &&) noexcept = default;
    GridInventory &operator=(GridInventory &&) noexcept = default;

    [[nodiscard]]
    int width() const noexcept;

    [[nodiscard]]
    int height() const noexcept;

    [[nodiscard]]
    std::size_t cellCount() const noexcept;

    [[nodiscard]]
    bool canPlace(
        ItemId definitionId,
        GridPosition origin,
        ItemOrientation orientation =
            ItemOrientation::Degrees0) const;

    [[nodiscard]]
    std::optional<GridPosition> findFirstFit(
        ItemId definitionId,
        ItemOrientation orientation =
            ItemOrientation::Degrees0) const;

    // 为即将到来的事务式放置预留元素容量。
    // 分配失败时 Inventory 的可观察内容保持不变。
    void reserveForAdditionalItems(
        std::size_t additionalItemCount);

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

    // 同时查询位置与方向变化，不修改 Inventory。
    [[nodiscard]]
    bool canTransform(
        ItemInstanceId instanceId,
        GridPosition newOrigin,
        ItemOrientation newOrientation) const;

    // 将已放置物品移动到 newOrigin。
    //
    // 成功时：
    // - 清除旧 footprint；
    // - 写入新 footprint；
    // - 更新 PlacedItem::origin；
    // - ItemInstance 和 instanceId 保持不变。
    //
    // 失败时 Inventory 完全不变。
    [[nodiscard]]
    bool tryMove(
        ItemInstanceId instanceId,
        GridPosition newOrigin);

    // 原子提交新位置与新方向；失败时 cells、origin 和 orientation 不变。
    [[nodiscard]]
    bool tryTransform(
        ItemInstanceId instanceId,
        GridPosition newOrigin,
        ItemOrientation newOrientation);

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

    // 显式销毁当前 Inventory 拥有的全部 ItemInstance，并清空占用表。
    // 不改变固定网格尺寸或已预留容量。
    void clear() noexcept;

    [[nodiscard]]
    std::optional<ItemInstanceId> occupantAt(
        GridPosition position) const noexcept;

    // 按稳定 ID 查询物品当前左上角格子。
    // 找不到时返回 nullopt，不暴露 placedItems_ 的内部迭代器。
    [[nodiscard]]
    std::optional<GridPosition> originOf(
        ItemInstanceId instanceId) const noexcept;

    [[nodiscard]]
    std::optional<std::uint32_t> quantityOf(
        ItemInstanceId instanceId) const noexcept;

    // Narrow mutation primitive used by prevalidated multi-stack
    // transactions. It never changes placement or cell occupancy.
    [[nodiscard]]
    bool trySetItemQuantity(
        ItemInstanceId instanceId,
        std::uint32_t quantity);

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
        ItemOrientation orientation,
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
        ItemOrientation orientation,
        std::optional<ItemInstanceId> occupant) noexcept;

    // 只能对已经确认合法的坐标调用。
    [[nodiscard]]
    std::size_t indexOf(GridPosition position) const noexcept;

    InventoryGridSize size_;
    std::vector<std::optional<ItemInstanceId>> cells_;
    std::vector<PlacedItem> placedItems_;
};
