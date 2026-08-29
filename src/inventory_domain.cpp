#include "inventory_domain.h"

#include "lost_raid_domain.h"

#include <algorithm>
#include <limits>
#include <set>

namespace
{
InventoryReceipt failure(
    DomainErrorCode error,
    std::string message,
    ProfileRevision revision)
{
    return InventoryReceipt{
        false,
        false,
        error,
        std::move(message),
        revision};
}

std::set<AssetInstanceId> overlappingAssets(
    const ProfileState &profile,
    const ContentRegistry &content,
    const StoredAssetLocation &destination,
    const ItemDefinition &definition,
    ItemOrientation orientation,
    AssetInstanceId ignored)
{
    std::set<AssetInstanceId> result;
    const InventoryFootprint footprint =
        inventoryFootprint(definition, orientation);
    for (int y = 0; y < footprint.height; ++y)
    {
        for (int x = 0; x < footprint.width; ++x)
        {
            const auto occupant = profileAssetAtCell(
                profile,
                content,
                destination.container,
                GridPosition{
                    destination.origin.x + x,
                    destination.origin.y + y});
            if (occupant.has_value() && *occupant != ignored)
            {
                result.insert(*occupant);
            }
        }
    }
    return result;
}

bool validateCandidate(
    const ProfileState &candidate,
    const ContentRegistry &content,
    InventoryReceipt &receipt,
    bool validateWholeProfile)
{
    if (validateWholeProfile)
    {
        const ProfileValidationResult validation =
            validateProfileState(candidate, content);
        if (!validation.valid)
        {
            receipt = failure(
                DomainErrorCode::IllegalDestination,
                validation.message,
                candidate.revision);
            return false;
        }
        return true;
    }

    AssetInstanceId maximumId{};
    std::set<EquipmentSlotKind> occupiedSlots;
    std::set<AssetInstanceId> installedWeaponIds;
    for (const auto &[id, asset] : candidate.assets.records())
    {
        if (id == 0 || id != asset.instanceId)
        {
            receipt = failure(
                DomainErrorCode::IllegalDestination,
                "asset ID is invalid",
                candidate.revision);
            return false;
        }
        maximumId = std::max(maximumId, id);

        const ItemDefinition *definition{};
        try
        {
            definition = &content.item(asset.definitionId);
        }
        catch (...)
        {
            receipt = failure(
                DomainErrorCode::IllegalDestination,
                "asset definition is unknown",
                candidate.revision);
            return false;
        }
        if (asset.quantity == 0 ||
            asset.quantity > definition->maxStackSize ||
            !canUseItemOrientation(*definition, asset.orientation))
        {
            receipt = failure(
                DomainErrorCode::IllegalDestination,
                "asset value is outside definition limits",
                candidate.revision);
            return false;
        }

        if (const auto *equipped =
                std::get_if<EquippedAssetLocation>(&asset.location))
        {
            if (!itemCanEquipInSlot(*definition, equipped->slot) ||
                !occupiedSlots.insert(equipped->slot).second)
            {
                receipt = failure(
                    DomainErrorCode::IllegalDestination,
                    "equipment slot ownership is invalid",
                    candidate.revision);
                return false;
            }
            continue;
        }

        if (const auto *installed =
                std::get_if<InstalledMagazineLocation>(&asset.location))
        {
            const AssetRecord *weapon =
                candidate.assets.find(installed->weaponAssetId);
            const ItemDefinition *weaponDefinition{};
            try
            {
                if (weapon != nullptr)
                {
                    weaponDefinition = &content.item(weapon->definitionId);
                }
            }
            catch (...)
            {
            }
            if (definition->category != ItemCategory::Magazine ||
                weaponDefinition == nullptr ||
                !installedWeaponIds.insert(
                    installed->weaponAssetId).second ||
                !weaponDefinition->compatibleMagazineDefinitionId.has_value() ||
                *weaponDefinition->compatibleMagazineDefinitionId !=
                    definition->definitionId)
            {
                receipt = failure(
                    DomainErrorCode::IllegalDestination,
                    "installed magazine ownership is invalid",
                    candidate.revision);
                return false;
            }
            continue;
        }

        const auto *stored =
            std::get_if<StoredAssetLocation>(&asset.location);
        if (stored == nullptr)
        {
            continue;
        }
        if (!profilePlacementFits(
                candidate,
                content,
                stored->container,
                stored->origin,
                *definition,
                asset.orientation,
                id))
        {
            receipt = failure(
                DomainErrorCode::IllegalDestination,
                "asset placement is invalid",
                candidate.revision);
            return false;
        }
        if (stored->container.kind == ProfileContainerKind::AssetCompartment)
        {
            if (stored->container.ownerAssetId == id)
            {
                receipt = failure(
                    DomainErrorCode::IllegalDestination,
                    "container owns itself",
                    candidate.revision);
                return false;
            }
            if (!definition->containerCompartments.empty())
            {
                const bool hasContents = std::any_of(
                    candidate.assets.records().begin(),
                    candidate.assets.records().end(),
                    [id](const auto &entry)
                    {
                        const auto *childStored =
                            std::get_if<StoredAssetLocation>(
                                &entry.second.location);
                        return childStored != nullptr &&
                            childStored->container.kind ==
                                ProfileContainerKind::AssetCompartment &&
                            childStored->container.ownerAssetId == id;
                    });
                if (hasContents)
                {
                    receipt = failure(
                        DomainErrorCode::IllegalDestination,
                        "non-empty container is nested",
                        candidate.revision);
                    return false;
                }
            }
        }
    }
    if (candidate.assets.nextAssetId() <= maximumId)
    {
        receipt = failure(
            DomainErrorCode::IllegalDestination,
            "asset high-water mark moved backward",
            candidate.revision);
        return false;
    }
    return true;
}

InventoryReceipt applyMove(
    ProfileState &candidate,
    const ContentRegistry &content,
    const InventoryMoveCommand &command,
    bool validateWholeProfile)
{
    AssetRecord *source = candidate.assets.findMutable(command.instanceId);
    if (source == nullptr)
    {
        return failure(
            DomainErrorCode::MissingAsset,
            "asset does not exist",
            candidate.revision);
    }
    if (lostRaidRecordForAsset(candidate, source->instanceId).has_value())
    {
        return failure(
            DomainErrorCode::IllegalDestination,
            "lost Raid assets require a recovery transaction",
            candidate.revision);
    }

    const ItemDefinition &definition = content.item(source->definitionId);
    const std::uint32_t requested =
        command.quantity == 0 ? source->quantity : command.quantity;
    if (requested == 0 || requested > source->quantity)
    {
        return failure(
            DomainErrorCode::InvalidQuantity,
            "requested quantity is outside the source stack",
            candidate.revision);
    }
    if (!canUseItemOrientation(definition, command.destinationOrientation))
    {
        return failure(
            DomainErrorCode::IllegalDestination,
            "requested orientation is not supported",
            candidate.revision);
    }
    const auto *sourceStored =
        std::get_if<StoredAssetLocation>(&source->location);
    const bool sourceInIntake = sourceStored != nullptr &&
        sourceStored->container == ProfileContainerId::baseIntake();
    if (command.destination.container ==
            ProfileContainerId::baseIntake() &&
        !sourceInIntake)
    {
        return failure(
            DomainErrorCode::IllegalDestination,
            "only Settlement can place assets into pending allocation",
            candidate.revision);
    }

    InventoryGridSize destinationSize{};
    try
    {
        destinationSize = profileContainerSize(
            candidate,
            content,
            command.destination.container);
    }
    catch (...)
    {
        return failure(
            DomainErrorCode::IllegalDestination,
            "destination container does not exist",
            candidate.revision);
    }
    const InventoryFootprint footprint =
        inventoryFootprint(definition, command.destinationOrientation);
    if (command.destination.origin.x < 0 ||
        command.destination.origin.y < 0 ||
        footprint.width <= 0 || footprint.height <= 0 ||
        command.destination.origin.x > destinationSize.width - footprint.width ||
        command.destination.origin.y > destinationSize.height - footprint.height)
    {
        return failure(
            DomainErrorCode::Capacity,
            "asset does not fit inside the destination",
            candidate.revision);
    }

    const AssetLocation originalLocation = source->location;
    const ItemOrientation originalOrientation = source->orientation;
    const std::set<AssetInstanceId> overlaps = overlappingAssets(
        candidate,
        content,
        command.destination,
        definition,
        command.destinationOrientation,
        source->instanceId);
    if (sourceInIntake && !overlaps.empty())
    {
        const AssetRecord *target = overlaps.size() == 1
            ? candidate.assets.find(*overlaps.begin())
            : nullptr;
        if (target == nullptr ||
            target->definitionId != source->definitionId ||
            target->reliefBatchId != source->reliefBatchId ||
            definition.maxStackSize <= 1 ||
            target->quantity > definition.maxStackSize - requested)
        {
            return failure(
                DomainErrorCode::IllegalDestination,
                "pending allocation cannot swap an asset back into intake",
                candidate.revision);
        }
    }

    if (requested < source->quantity)
    {
        if (definition.maxStackSize <= 1 || overlaps.size() > 1)
        {
            return failure(
                DomainErrorCode::InvalidQuantity,
                "partial movement requires a stackable empty or matching target",
                candidate.revision);
        }

        if (overlaps.empty())
        {
            source->quantity -= requested;
            const AssetInstanceId splitId = candidate.assets.create(
                definition,
                command.destination,
                requested,
                source->reliefBatchId);
            AssetRecord *split = candidate.assets.findMutable(splitId);
            split->orientation = command.destinationOrientation;
        }
        else
        {
            AssetRecord *target = candidate.assets.findMutable(*overlaps.begin());
            if (target == nullptr || target->definitionId != source->definitionId ||
                target->reliefBatchId != source->reliefBatchId ||
                target->quantity > definition.maxStackSize - requested)
            {
                return failure(
                    DomainErrorCode::Capacity,
                    "selected quantity cannot be merged completely",
                    candidate.revision);
            }
            target->quantity += requested;
            source->quantity -= requested;
        }
    }
    else if (overlaps.empty())
    {
        source->location = command.destination;
        source->orientation = command.destinationOrientation;
    }
    else if (overlaps.size() == 1)
    {
        AssetRecord *target = candidate.assets.findMutable(*overlaps.begin());
        if (target == nullptr)
        {
            return failure(
                DomainErrorCode::MissingAsset,
                "destination asset disappeared",
                candidate.revision);
        }

        if (target->definitionId == source->definitionId &&
            target->reliefBatchId != source->reliefBatchId &&
            definition.maxStackSize > 1)
        {
            return failure(
                DomainErrorCode::IllegalDestination,
                "stacks from different relief batches cannot be combined",
                candidate.revision);
        }

        if (target->definitionId == source->definitionId &&
            target->reliefBatchId == source->reliefBatchId &&
            definition.maxStackSize > 1)
        {
            const std::uint32_t available =
                definition.maxStackSize - target->quantity;
            if (available == 0)
            {
                return failure(
                    DomainErrorCode::Capacity,
                    "destination stack is full",
                    candidate.revision);
            }
            const std::uint32_t transferred =
                std::min(available, source->quantity);
            target->quantity += transferred;
            source->quantity -= transferred;
            if (source->quantity == 0)
            {
                static_cast<void>(
                    candidate.assets.erase(source->instanceId));
            }
        }
        else
        {
            source->location = command.destination;
            source->orientation = command.destinationOrientation;
            target->location = originalLocation;
        }
    }
    else
    {
        return failure(
            DomainErrorCode::Capacity,
            "destination overlaps more than one asset",
            candidate.revision);
    }

    InventoryReceipt receipt{true, false, DomainErrorCode::None, {}, candidate.revision};
    if (!validateCandidate(
            candidate, content, receipt, validateWholeProfile))
    {
        return receipt;
    }
    return receipt;
}

InventoryReceipt applyEquip(
    ProfileState &candidate,
    const ContentRegistry &content,
    const InventoryEquipCommand &command,
    bool validateWholeProfile)
{
    AssetRecord *source = candidate.assets.findMutable(command.instanceId);
    if (source == nullptr)
    {
        return failure(
            DomainErrorCode::MissingAsset,
            "asset does not exist",
            candidate.revision);
    }
    if (lostRaidRecordForAsset(candidate, source->instanceId).has_value())
    {
        return failure(
            DomainErrorCode::IllegalDestination,
            "lost Raid assets require a recovery transaction",
            candidate.revision);
    }
    const ItemDefinition &definition = content.item(source->definitionId);
    if (const auto *stored =
            std::get_if<StoredAssetLocation>(&source->location);
        stored != nullptr &&
        stored->container == ProfileContainerId::baseIntake())
    {
        return failure(
            DomainErrorCode::IllegalDestination,
            "pending allocation must be kept before it can be equipped",
            candidate.revision);
    }
    if (!itemCanEquipInSlot(definition, command.slot))
    {
        return failure(
            DomainErrorCode::IncompatibleEquipment,
            "asset cannot use the requested equipment slot",
            candidate.revision);
    }

    if (const auto *equipped =
            std::get_if<EquippedAssetLocation>(&source->location);
        equipped != nullptr && equipped->slot == command.slot)
    {
        return InventoryReceipt{
            true, false, DomainErrorCode::None, {}, candidate.revision};
    }

    const AssetLocation originalLocation = source->location;
    const auto occupied = equippedAsset(candidate, command.slot);
    source->location = EquippedAssetLocation{command.slot};
    if (occupied.has_value() && *occupied != source->instanceId)
    {
        AssetRecord *target = candidate.assets.findMutable(*occupied);
        if (target == nullptr ||
            !std::holds_alternative<StoredAssetLocation>(originalLocation))
        {
            return failure(
                DomainErrorCode::Capacity,
                "occupied equipment slot cannot be swapped",
                candidate.revision);
        }
        target->location = originalLocation;
    }

    InventoryReceipt receipt{true, false, DomainErrorCode::None, {}, candidate.revision};
    if (!validateCandidate(
            candidate, content, receipt, validateWholeProfile))
    {
        return receipt;
    }
    return receipt;
}

InventoryReceipt apply(
    ProfileState &candidate,
    const ContentRegistry &content,
    const InventoryCommand &command,
    bool validateWholeProfile)
{
    return std::visit(
        [&candidate, &content, validateWholeProfile](
            const auto &typedCommand)
        {
            using Command = std::decay_t<decltype(typedCommand)>;
            if constexpr (std::is_same_v<Command, InventoryMoveCommand>)
            {
                return applyMove(
                    candidate,
                    content,
                    typedCommand,
                    validateWholeProfile);
            }
            else
            {
                return applyEquip(
                    candidate,
                    content,
                    typedCommand,
                    validateWholeProfile);
            }
        },
        command);
}
}

InventoryPlan queryInventory(
    const ProfileState &profile,
    const ContentRegistry &content,
    const InventoryCommand &command)
{
    ProfileState candidate;
    candidate.revision = profile.revision;
    candidate.assets = profile.assets;
    const InventoryReceipt receipt = apply(
        candidate, content, command, false);
    return InventoryPlan{
        receipt.succeeded,
        receipt.error,
        receipt.message,
        profile.revision};
}

InventoryReceipt executeInventory(
    ProfileState &profile,
    const ContentRegistry &content,
    const InventoryCommand &command,
    const CommandContext &context)
{
    if (context.transactionId.empty())
    {
        return failure(
            DomainErrorCode::InvalidTransaction,
            "transaction ID must not be empty",
            profile.revision);
    }
    if (profile.committedTransactions.contains(context.transactionId))
    {
        return InventoryReceipt{
            true, true, DomainErrorCode::None, {}, profile.revision};
    }
    if (context.expectedRevision != profile.revision)
    {
        return failure(
            DomainErrorCode::StaleRevision,
            "profile revision is stale",
            profile.revision);
    }
    if (profile.revision == std::numeric_limits<ProfileRevision>::max())
    {
        return failure(
            DomainErrorCode::RevisionOverflow,
            "profile revision cannot advance",
            profile.revision);
    }

    AssetRegistry originalAssets = profile.assets;
    std::set<std::string> committedTransactions =
        profile.committedTransactions;
    committedTransactions.insert(context.transactionId);

    InventoryReceipt receipt;
    try
    {
        // Inventory commands can only mutate the asset registry. Validate that
        // bounded participant graph here; the frozen Raid layout and the other
        // profile aggregates are validated at deploy, persistence, recovery,
        // and settlement boundaries instead of rescanning the megamap for
        // every pickup or drag.
        receipt = apply(profile, content, command, false);
    }
    catch (...)
    {
        profile.assets = std::move(originalAssets);
        throw;
    }
    if (!receipt.succeeded)
    {
        profile.assets = std::move(originalAssets);
        receipt.revision = profile.revision;
        return receipt;
    }

    profile.committedTransactions = std::move(committedTransactions);
    ++profile.revision;
    receipt.revision = profile.revision;
    return receipt;
}
