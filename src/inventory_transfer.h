#pragma once

#include <cstdint>
#include <optional>

#include "grid_inventory.h"

// 跨两个不同 GridInventory 的无副作用放置查询。
[[nodiscard]]
bool canTransferItem(
    const GridInventory &source,
    const GridInventory &destination,
    ItemInstanceId instanceId,
    GridPosition destinationOrigin);

[[nodiscard]]
bool canTransferItemTransform(
    const GridInventory &source,
    const GridInventory &destination,
    ItemInstanceId instanceId,
    GridPosition destinationOrigin,
    ItemOrientation destinationOrientation);

// 跨两个不同 GridInventory 的事务式所有权转移。
// 失败时两个 Inventory 的可观察状态均保持不变。
[[nodiscard]]
bool tryTransferItem(
    GridInventory &source,
    GridInventory &destination,
    ItemInstanceId instanceId,
    GridPosition destinationOrigin);

[[nodiscard]]
bool tryTransferItemTransform(
    GridInventory &source,
    GridInventory &destination,
    ItemInstanceId instanceId,
    GridPosition destinationOrigin,
    ItemOrientation destinationOrientation);

// 按行优先查询目标容器中的第一个合法位置。
[[nodiscard]]
std::optional<GridPosition> findFirstTransferFit(
    const GridInventory &source,
    const GridInventory &destination,
    ItemInstanceId instanceId);

// 使用 findFirstTransferFit 的确定性位置执行事务式转移。
[[nodiscard]]
bool tryTransferItemFirstFit(
    GridInventory &source,
    GridInventory &destination,
    ItemInstanceId instanceId);

// 从源容器的任意占用格解析稳定 ID，再执行确定性的 first-fit 转移。
// 空格、越界格或目标无容量时，两个容器都保持不变。
[[nodiscard]]
bool tryTransferItemAtCellFirstFit(
    GridInventory &source,
    GridInventory &destination,
    GridPosition sourceCell);

struct QuantityTransferResult
{
    bool succeeded{};
    bool consumedSplitInstanceId{};

    friend bool operator==(
        const QuantityTransferResult &,
        const QuantityTransferResult &) = default;
};

// Transfers exactly requestedQuantity or leaves both inventories unchanged.
// splitInstanceId is consumed only when a partial transfer must create a new
// destination placement. Merge targets retain their existing stable IDs.
[[nodiscard]]
QuantityTransferResult tryTransferItemQuantityFirstFit(
    GridInventory &source,
    GridInventory &destination,
    ItemInstanceId instanceId,
    std::uint32_t requestedQuantity,
    ItemInstanceId splitInstanceId);

// Places exactly requestedQuantity at the pointer-selected destination cell.
// An empty cell receives a placement; a matching occupied stack receives an
// exact merge. The source and destination may be the same inventory. Failure
// leaves every placement, quantity and stable ID unchanged.
[[nodiscard]]
bool canPlaceItemQuantityAt(
    const GridInventory &source,
    const GridInventory &destination,
    ItemInstanceId instanceId,
    std::uint32_t requestedQuantity,
    GridPosition destinationOrigin,
    ItemOrientation destinationOrientation);

[[nodiscard]]
QuantityTransferResult tryPlaceItemQuantityAt(
    GridInventory &source,
    GridInventory &destination,
    ItemInstanceId instanceId,
    std::uint32_t requestedQuantity,
    GridPosition destinationOrigin,
    ItemOrientation destinationOrientation,
    ItemInstanceId splitInstanceId);

// Inserts one complete stack, merging in stable placement order before using
// row-major first-fit. Failure does not move or otherwise mutate item.
[[nodiscard]]
bool tryInsertItemFirstFit(
    GridInventory &destination,
    ItemInstance &&item);
