#include "inventory_transfer.h"

#include <algorithm>
#include <exception>
#include <utility>

namespace
{

    const PlacedItem *findPlacedItem(
        const GridInventory &inventory,
        ItemInstanceId instanceId) noexcept
    {
        const auto &placedItems =
            inventory.placedItems();

        const auto placedIt = std::find_if(
            placedItems.begin(),
            placedItems.end(),
            [instanceId](const PlacedItem &placed)
            {
                return placed.item.instanceId() ==
                       instanceId;
            });

        if (placedIt == placedItems.end())
        {
            return nullptr;
        }

        return &*placedIt;
    }

    bool containsInstanceId(
        const GridInventory &inventory,
        ItemInstanceId instanceId) noexcept
    {
        return findPlacedItem(
                   inventory,
                   instanceId) != nullptr;
    }

} // namespace

bool canTransferItem(
    const GridInventory &source,
    const GridInventory &destination,
    ItemInstanceId instanceId,
    GridPosition destinationOrigin)
{
    if (&source == &destination)
    {
        return false;
    }

    const PlacedItem *sourceItem =
        findPlacedItem(source, instanceId);

    if (sourceItem == nullptr)
    {
        return false;
    }

    if (containsInstanceId(destination, instanceId))
    {
        return false;
    }

    return destination.canPlace(
        sourceItem->item.definitionId(),
        destinationOrigin);
}

bool tryTransferItem(
    GridInventory &source,
    GridInventory &destination,
    ItemInstanceId instanceId,
    GridPosition destinationOrigin)
{
    if (!canTransferItem(
            source,
            destination,
            instanceId,
            destinationOrigin))
    {
        return false;
    }

    const std::optional<GridPosition> sourceOrigin =
        source.originOf(instanceId);

    if (!sourceOrigin.has_value())
    {
        return false;
    }

    // Any allocation happens before the source inventory is mutated.
    destination.reserveForAdditionalItems(1);

    std::optional<ItemInstance> transferred =
        source.remove(instanceId);

    if (!transferred.has_value())
    {
        return false;
    }

    // Capacity and placement legality were checked before this no-allocation
    // commit path.
    if (destination.tryPlace(
            std::move(*transferred),
            destinationOrigin))
    {
        return true;
    }

    // Defensive rollback: unreachable for normal single-threaded callers, but
    // an item must never disappear silently.
    const bool restored = source.tryPlace(
        std::move(*transferred),
        *sourceOrigin);

    if (!restored)
    {
        std::terminate();
    }

    return false;
}

std::optional<GridPosition> findFirstTransferFit(
    const GridInventory &source,
    const GridInventory &destination,
    ItemInstanceId instanceId)
{
    if (&source == &destination)
    {
        return std::nullopt;
    }

    const PlacedItem *sourceItem =
        findPlacedItem(source, instanceId);

    if (sourceItem == nullptr ||
        containsInstanceId(destination, instanceId))
    {
        return std::nullopt;
    }

    return destination.findFirstFit(
        sourceItem->item.definitionId());
}

bool tryTransferItemFirstFit(
    GridInventory &source,
    GridInventory &destination,
    ItemInstanceId instanceId)
{
    const std::optional<GridPosition> destinationOrigin =
        findFirstTransferFit(
            source,
            destination,
            instanceId);

    if (!destinationOrigin.has_value())
    {
        return false;
    }

    return tryTransferItem(
        source,
        destination,
        instanceId,
        *destinationOrigin);
}
