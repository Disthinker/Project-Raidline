#include "stash.h"

#include "inventory_transfer.h"

namespace
{

    constexpr InventoryGridSize kDefaultStashSize{
        20,
        12};

}

Stash::Stash()
    : Stash(kDefaultStashSize)
{
}

Stash::Stash(
    InventoryGridSize size)
    : inventory_(size)
{
}

bool Stash::canStoreAll(
    const GridInventory &source) const
{
    return canTransferAllItemsFirstFit(
        source,
        inventory_);
}

bool Stash::tryStoreAll(
    GridInventory &source)
{
    return tryTransferAllItemsFirstFit(
        source,
        inventory_);
}

GridInventory &Stash::inventory() noexcept
{
    return inventory_;
}

const GridInventory &Stash::inventory() const noexcept
{
    return inventory_;
}

std::size_t Stash::stackCount() const noexcept
{
    return inventory_.placedItems().size();
}

std::uint64_t Stash::unitCount() const noexcept
{
    std::uint64_t total{};

    for (const PlacedItem &placed :
         inventory_.placedItems())
    {
        total += placed.item.quantity();
    }

    return total;
}
