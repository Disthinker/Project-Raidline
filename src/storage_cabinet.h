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
    GridInventory &inventory() noexcept;

    [[nodiscard]]
    const GridInventory &inventory() const noexcept;

private:
    Vec2 position_{};
    Vec2 size_{};
    float interactionPadding_{};
    GridInventory inventory_;
};
