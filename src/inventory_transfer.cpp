#include "inventory_transfer.h"

#include <algorithm>
#include <cstdint>
#include <exception>
#include <optional>
#include <utility>
#include <vector>

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

    struct StackFill
    {
        ItemInstanceId instanceId{};
        std::uint32_t finalQuantity{};
    };

    struct StackInsertionPlan
    {
        std::vector<StackFill> fills;
        std::optional<GridPosition> newStackOrigin;
        std::uint32_t newStackQuantity{};
    };

    std::optional<StackInsertionPlan>
    makeStackInsertionPlan(
        const GridInventory &destination,
        ItemId definitionId,
        ItemOrientation orientation,
        std::uint32_t requestedQuantity)
    {
        if (requestedQuantity == 0)
        {
            return std::nullopt;
        }

        const ItemDefinition &definition =
            itemDefinition(definitionId);

        StackInsertionPlan plan;
        std::uint32_t remaining =
            requestedQuantity;

        for (const PlacedItem &placed :
             destination.placedItems())
        {
            if (remaining == 0)
            {
                break;
            }

            if (placed.item.definitionId() != definitionId ||
                placed.item.quantity() >= definition.maxStackSize)
            {
                continue;
            }

            const std::uint32_t available =
                definition.maxStackSize -
                placed.item.quantity();
            const std::uint32_t amount =
                std::min(available, remaining);

            plan.fills.push_back(
                StackFill{
                    placed.item.instanceId(),
                    placed.item.quantity() + amount});
            remaining -= amount;
        }

        if (remaining == 0)
        {
            return plan;
        }

        const std::optional<GridPosition> origin =
            destination.findFirstFit(
                definitionId,
                orientation);

        if (!origin.has_value())
        {
            return std::nullopt;
        }

        plan.newStackOrigin = origin;
        plan.newStackQuantity = remaining;
        return plan;
    }

    void commitStackFills(
        GridInventory &destination,
        const std::vector<StackFill> &fills)
    {
        for (const StackFill &fill : fills)
        {
            if (!destination.trySetItemQuantity(
                    fill.instanceId,
                    fill.finalQuantity))
            {
                std::terminate();
            }
        }
    }

} // namespace

bool canTransferItem(
    const GridInventory &source,
    const GridInventory &destination,
    ItemInstanceId instanceId,
    GridPosition destinationOrigin)
{
    const PlacedItem *sourceItem =
        findPlacedItem(source, instanceId);

    if (sourceItem == nullptr)
    {
        return false;
    }

    return canTransferItemTransform(
        source,
        destination,
        instanceId,
        destinationOrigin,
        sourceItem->item.orientation());
}

bool canTransferItemTransform(
    const GridInventory &source,
    const GridInventory &destination,
    ItemInstanceId instanceId,
    GridPosition destinationOrigin,
    ItemOrientation destinationOrientation)
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
        destinationOrigin,
        destinationOrientation);
}

bool tryTransferItem(
    GridInventory &source,
    GridInventory &destination,
    ItemInstanceId instanceId,
    GridPosition destinationOrigin)
{
    const PlacedItem *sourceItem =
        findPlacedItem(source, instanceId);

    if (sourceItem == nullptr)
    {
        return false;
    }

    return tryTransferItemTransform(
        source,
        destination,
        instanceId,
        destinationOrigin,
        sourceItem->item.orientation());
}

bool tryTransferItemTransform(
    GridInventory &source,
    GridInventory &destination,
    ItemInstanceId instanceId,
    GridPosition destinationOrigin,
    ItemOrientation destinationOrientation)
{
    if (!canTransferItemTransform(
            source,
            destination,
            instanceId,
            destinationOrigin,
            destinationOrientation))
    {
        return false;
    }

    const std::optional<GridPosition> sourceOrigin =
        source.originOf(instanceId);

    if (!sourceOrigin.has_value())
    {
        return false;
    }

    const PlacedItem *sourceItem =
        findPlacedItem(source, instanceId);

    if (sourceItem == nullptr)
    {
        return false;
    }

    const ItemOrientation sourceOrientation =
        sourceItem->item.orientation();

    // Any allocation happens before the source inventory is mutated.
    destination.reserveForAdditionalItems(1);

    std::optional<ItemInstance> transferred =
        source.remove(instanceId);

    if (!transferred.has_value())
    {
        return false;
    }

    if (!transferred->trySetOrientation(
            destinationOrientation))
    {
        const bool restored = source.tryPlace(
            std::move(*transferred),
            *sourceOrigin);

        if (!restored)
        {
            std::terminate();
        }

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
    const bool orientationRestored =
        transferred->trySetOrientation(
            sourceOrientation);

    if (!orientationRestored)
    {
        std::terminate();
    }

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
        sourceItem->item.definitionId(),
        sourceItem->item.orientation());
}

bool tryTransferItemFirstFit(
    GridInventory &source,
    GridInventory &destination,
    ItemInstanceId instanceId)
{
    const PlacedItem *sourceItem =
        findPlacedItem(source, instanceId);

    if (sourceItem == nullptr)
    {
        return false;
    }

    return tryTransferItemQuantityFirstFit(
               source,
               destination,
               instanceId,
               sourceItem->item.quantity(),
               0)
        .succeeded;
}

bool tryTransferItemAtCellFirstFit(
    GridInventory &source,
    GridInventory &destination,
    GridPosition sourceCell)
{
    const std::optional<ItemInstanceId> instanceId =
        source.occupantAt(sourceCell);

    if (!instanceId.has_value())
    {
        return false;
    }

    return tryTransferItemFirstFit(
        source,
        destination,
        *instanceId);
}

QuantityTransferResult tryTransferItemQuantityFirstFit(
    GridInventory &source,
    GridInventory &destination,
    ItemInstanceId instanceId,
    std::uint32_t requestedQuantity,
    ItemInstanceId splitInstanceId)
{
    if (&source == &destination)
    {
        return {};
    }

    const PlacedItem *sourceItem =
        findPlacedItem(source, instanceId);

    if (sourceItem == nullptr ||
        requestedQuantity == 0 ||
        requestedQuantity > sourceItem->item.quantity() ||
        containsInstanceId(destination, instanceId))
    {
        return {};
    }

    const std::uint32_t sourceQuantity =
        sourceItem->item.quantity();
    const bool partial =
        requestedQuantity < sourceQuantity;
    const ItemId definitionId =
        sourceItem->item.definitionId();
    const ItemOrientation orientation =
        sourceItem->item.orientation();

    std::optional<StackInsertionPlan> plan =
        makeStackInsertionPlan(
            destination,
            definitionId,
            orientation,
            requestedQuantity);

    if (!plan.has_value())
    {
        return {};
    }

    const bool createsNewStack =
        plan->newStackOrigin.has_value();
    const bool consumesSplitId =
        partial && createsNewStack;

    if (consumesSplitId &&
        (splitInstanceId == 0 ||
         splitInstanceId == instanceId ||
         containsInstanceId(source, splitInstanceId) ||
         containsInstanceId(destination, splitInstanceId)))
    {
        return {};
    }

    std::optional<ItemInstance> splitStack;
    if (consumesSplitId)
    {
        splitStack.emplace(
            splitInstanceId,
            definitionId,
            plan->newStackQuantity);

        if (!splitStack->trySetOrientation(orientation))
        {
            return {};
        }
    }

    if (createsNewStack)
    {
        destination.reserveForAdditionalItems(1);
    }

    std::optional<ItemInstance> removedSource;

    if (partial)
    {
        if (!source.trySetItemQuantity(
                instanceId,
                sourceQuantity - requestedQuantity))
        {
            std::terminate();
        }
    }
    else
    {
        removedSource = source.remove(instanceId);
        if (!removedSource.has_value())
        {
            std::terminate();
        }
    }

    commitStackFills(
        destination,
        plan->fills);

    if (createsNewStack)
    {
        ItemInstance *newStack = nullptr;

        if (partial)
        {
            newStack = &*splitStack;
        }
        else
        {
            if (!removedSource->trySetQuantity(
                    plan->newStackQuantity))
            {
                std::terminate();
            }

            newStack = &*removedSource;
        }

        if (!destination.tryPlace(
                std::move(*newStack),
                *plan->newStackOrigin))
        {
            std::terminate();
        }
    }

    return QuantityTransferResult{
        true,
        consumesSplitId};
}

bool canPlaceItemQuantityAt(
    const GridInventory &source,
    const GridInventory &destination,
    ItemInstanceId instanceId,
    std::uint32_t requestedQuantity,
    GridPosition destinationOrigin,
    ItemOrientation destinationOrientation)
{
    const PlacedItem *sourceItem =
        findPlacedItem(source, instanceId);

    if (sourceItem == nullptr ||
        requestedQuantity == 0 ||
        requestedQuantity > sourceItem->item.quantity() ||
        (&source != &destination &&
         containsInstanceId(destination, instanceId)))
    {
        return false;
    }

    const ItemDefinition &definition =
        itemDefinition(sourceItem->item.definitionId());

    if (definition.maxStackSize <= 1 ||
        !canUseItemOrientation(
            definition,
            destinationOrientation))
    {
        return false;
    }

    const std::optional<ItemInstanceId> destinationId =
        destination.occupantAt(destinationOrigin);

    if (!destinationId.has_value())
    {
        return destination.canPlace(
            sourceItem->item.definitionId(),
            destinationOrigin,
            destinationOrientation);
    }

    if (&source == &destination &&
        *destinationId == instanceId)
    {
        return true;
    }

    const PlacedItem *destinationItem =
        findPlacedItem(destination, *destinationId);

    return destinationItem != nullptr &&
           destinationItem->item.definitionId() ==
               sourceItem->item.definitionId() &&
           requestedQuantity <=
               definition.maxStackSize -
                   destinationItem->item.quantity();
}

QuantityTransferResult tryPlaceItemQuantityAt(
    GridInventory &source,
    GridInventory &destination,
    ItemInstanceId instanceId,
    std::uint32_t requestedQuantity,
    GridPosition destinationOrigin,
    ItemOrientation destinationOrientation,
    ItemInstanceId splitInstanceId)
{
    if (!canPlaceItemQuantityAt(
            source,
            destination,
            instanceId,
            requestedQuantity,
            destinationOrigin,
            destinationOrientation))
    {
        return {};
    }

    const PlacedItem *sourceItem =
        findPlacedItem(source, instanceId);
    if (sourceItem == nullptr)
    {
        return {};
    }

    const std::uint32_t sourceQuantity =
        sourceItem->item.quantity();
    const ItemId definitionId =
        sourceItem->item.definitionId();
    const bool partial =
        requestedQuantity < sourceQuantity;
    const std::optional<ItemInstanceId> destinationId =
        destination.occupantAt(destinationOrigin);

    if (destinationId.has_value())
    {
        if (&source == &destination &&
            *destinationId == instanceId)
        {
            return QuantityTransferResult{true, false};
        }

        const std::optional<std::uint32_t> destinationQuantity =
            destination.quantityOf(*destinationId);
        if (!destinationQuantity.has_value())
        {
            std::terminate();
        }

        if (partial)
        {
            if (!source.trySetItemQuantity(
                    instanceId,
                    sourceQuantity - requestedQuantity))
            {
                std::terminate();
            }
        }
        else
        {
            std::optional<ItemInstance> removed =
                source.remove(instanceId);
            if (!removed.has_value())
            {
                std::terminate();
            }
        }

        if (!destination.trySetItemQuantity(
                *destinationId,
                *destinationQuantity + requestedQuantity))
        {
            std::terminate();
        }

        return QuantityTransferResult{true, false};
    }

    if (!partial)
    {
        const bool succeeded = &source == &destination
            ? source.tryTransform(
                  instanceId,
                  destinationOrigin,
                  destinationOrientation)
            : tryTransferItemTransform(
                  source,
                  destination,
                  instanceId,
                  destinationOrigin,
                  destinationOrientation);

        return QuantityTransferResult{succeeded, false};
    }

    if (splitInstanceId == 0 ||
        splitInstanceId == instanceId ||
        containsInstanceId(source, splitInstanceId) ||
        (&source != &destination &&
         containsInstanceId(destination, splitInstanceId)))
    {
        return {};
    }

    ItemInstance splitStack{
        splitInstanceId,
        definitionId,
        requestedQuantity};
    if (!splitStack.trySetOrientation(destinationOrientation))
    {
        return {};
    }

    destination.reserveForAdditionalItems(1);

    if (!source.trySetItemQuantity(
            instanceId,
            sourceQuantity - requestedQuantity))
    {
        std::terminate();
    }

    if (!destination.tryPlace(
            std::move(splitStack),
            destinationOrigin))
    {
        std::terminate();
    }

    return QuantityTransferResult{true, true};
}

bool tryInsertItemFirstFit(
    GridInventory &destination,
    ItemInstance &&item)
{
    if (!item.valid() ||
        containsInstanceId(
            destination,
            item.instanceId()))
    {
        return false;
    }

    std::optional<StackInsertionPlan> plan =
        makeStackInsertionPlan(
            destination,
            item.definitionId(),
            item.orientation(),
            item.quantity());

    if (!plan.has_value())
    {
        return false;
    }

    if (plan->newStackOrigin.has_value())
    {
        destination.reserveForAdditionalItems(1);

        if (!item.trySetQuantity(
                plan->newStackQuantity))
        {
            std::terminate();
        }

        commitStackFills(destination, plan->fills);

        if (!destination.tryPlace(
                std::move(item),
                *plan->newStackOrigin))
        {
            std::terminate();
        }

        return true;
    }

    ItemInstance consumed{std::move(item)};
    commitStackFills(destination, plan->fills);
    return consumed.valid();
}
