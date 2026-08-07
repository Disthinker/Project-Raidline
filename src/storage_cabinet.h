#pragma once

#include "grid_inventory.h"
#include "rect.h"

class StorageCabinet
{
public:
    StorageCabinet(
        Vec2 position,
        Vec2 size,
        float interactionPadding,
        InventoryGridSize inventorySize);

    [[nodiscard]]
    Vec2 position() const noexcept;

    [[nodiscard]]
    Vec2 size() const noexcept;

    [[nodiscard]]
    Rect bounds() const noexcept;

    [[nodiscard]]
    bool canInteract(Rect actorBounds) const noexcept;

    [[nodiscard]]
    bool isSearched() const noexcept;

    // 只允许未搜索的空柜体接收一次完整、同尺寸的搜索结果。
    // 失败时正式柜体库存和搜索状态保持不变。
    [[nodiscard]]
    bool tryCommitSearchResult(
        GridInventory &&searchResult) noexcept;

    [[nodiscard]]
    GridInventory &inventory() noexcept;

    [[nodiscard]]
    const GridInventory &inventory() const noexcept;

private:
    Vec2 position_{};
    Vec2 size_{};
    float interactionPadding_{};
    GridInventory inventory_;
    bool searched_{};
};
