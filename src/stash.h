#pragma once

#include <cstddef>
#include <cstdint>

#include "grid_inventory.h"

// 跨局仓库的最小领域边界。Week22 仅负责内存中的物品所有权，
// UI、持久化与下一局配装留给后续里程碑。
class Stash
{
public:
    Stash();

    explicit Stash(
        InventoryGridSize size);

    [[nodiscard]]
    bool canStoreAll(
        const GridInventory &source) const;

    // 完整接收 source 中的精确堆叠；失败时双方均不改变。
    [[nodiscard]]
    bool tryStoreAll(
        GridInventory &source);

    [[nodiscard]]
    GridInventory &inventory() noexcept;

    [[nodiscard]]
    const GridInventory &inventory() const noexcept;

    [[nodiscard]]
    std::size_t stackCount() const noexcept;

    [[nodiscard]]
    std::uint64_t unitCount() const noexcept;

private:
    GridInventory inventory_;
};
