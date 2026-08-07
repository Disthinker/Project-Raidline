#pragma once

#include <optional>

#include "grid_inventory.h"

// 跨两个不同 GridInventory 的无副作用放置查询。
[[nodiscard]]
bool canTransferItem(
    const GridInventory &source,
    const GridInventory &destination,
    ItemInstanceId instanceId,
    GridPosition destinationOrigin);

// 跨两个不同 GridInventory 的事务式所有权转移。
// 失败时两个 Inventory 的可观察状态均保持不变。
[[nodiscard]]
bool tryTransferItem(
    GridInventory &source,
    GridInventory &destination,
    ItemInstanceId instanceId,
    GridPosition destinationOrigin);

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
