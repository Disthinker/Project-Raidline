#include "weapon_ammo_domain.h"
#include "raid_corpse_search.h"
#include "tactical_device_domain.h"
#include "weapon_component_domain.h"
#include "inventory_domain.h"
#include <cmath>

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
    std::map<ProfileContainerId,std::set<std::pair<int,int>>> occupiedCells;
    std::set<AssetInstanceId> containersWithChildren;
    for(const auto &[id,asset]:candidate.assets.records())
        if(const auto *stored=std::get_if<StoredAssetLocation>(&asset.location);
           stored && stored->container.kind==ProfileContainerKind::AssetCompartment)
            containersWithChildren.insert(stored->container.ownerAssetId);
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
        if (bool(asset.fireMode) != bool(definition->weaponUse) ||
            (asset.fireMode && std::find(definition->weaponUse->supportedFireModes.begin(),
                definition->weaponUse->supportedFireModes.end(), *asset.fireMode) == definition->weaponUse->supportedFireModes.end()))
        { receipt = failure(DomainErrorCode::IllegalDestination, "unsupported weapon fire mode", candidate.revision); return false; }
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
                !content.magazineFitsWeapon(
                    definition->definitionId,
                    weaponDefinition->definitionId))
            {
                receipt = failure(
                    DomainErrorCode::IllegalDestination,
                    "installed magazine ownership is invalid",
                    candidate.revision);
                return false;
            }
            continue;
        }

        if(const auto *held=std::get_if<BaseReloadHeldMagazineLocation>(&asset.location))
        {
            const auto *parent=candidate.assets.find(held->weaponAssetId);
            if(!parent || definition->category!=ItemCategory::Magazine || held->actionId.empty() ||
                !std::holds_alternative<EquippedAssetLocation>(parent->location) ||
                !content.magazineFitsWeapon(asset.definitionId,parent->definitionId))
            {receipt=failure(DomainErrorCode::IllegalDestination,"invalid Base held magazine",candidate.revision);return false;}
            continue;
        }
        if(const auto *held=std::get_if<TrainingHeldMagazineLocation>(&asset.location))
        {
            const auto *parent=candidate.assets.find(held->weaponAssetId);
            if(!parent || definition->category!=ItemCategory::Magazine || held->sessionId.empty() || held->actionId.empty() ||
                !std::holds_alternative<EquippedAssetLocation>(parent->location) ||
                !content.magazineFitsWeapon(asset.definitionId,parent->definitionId))
            {receipt=failure(DomainErrorCode::IllegalDestination,"invalid training held magazine",candidate.revision);return false;}
            continue;
        }
        if(std::holds_alternative<TrainingDroppedMagazineLocation>(asset.location))
        {
            if(definition->category!=ItemCategory::Magazine)
            {receipt=failure(DomainErrorCode::IllegalDestination,"invalid training dropped magazine",candidate.revision);return false;}
            continue;
        }
        if (const auto *held = std::get_if<RaidReloadHeldMagazineLocation>(&asset.location))
        {
            const auto *parent = candidate.assets.find(held->weaponAssetId);
            if (!parent || definition->category != ItemCategory::Magazine || held->actionId.empty() ||
                !std::holds_alternative<EquippedAssetLocation>(parent->location) ||
                (held->role != RaidReloadMagazineRole::NewMagazine && held->role != RaidReloadMagazineRole::OldMagazine) ||
                !content.magazineFitsWeapon(asset.definitionId, parent->definitionId))
            { receipt = failure(DomainErrorCode::IllegalDestination, "invalid held magazine", candidate.revision); return false; }
            continue;
        }
        if (const auto *retained = std::get_if<RetainedRaidMagazineLocation>(&asset.location))
        {
            const auto *parent = candidate.assets.find(retained->weaponAssetId);
            if (!parent || definition->category != ItemCategory::Magazine ||
                (retained->role != RaidReloadMagazineRole::NewMagazine && retained->role != RaidReloadMagazineRole::OldMagazine) ||
                (!std::holds_alternative<LostRaidAssetLocation>(parent->location) &&
                 !std::holds_alternative<RecoveryTaskAssetLocation>(parent->location) &&
                 !std::holds_alternative<RaidGroundAssetLocation>(parent->location)) ||
                !content.magazineFitsWeapon(asset.definitionId, parent->definitionId))
            { receipt = failure(DomainErrorCode::IllegalDestination, "invalid retained magazine", candidate.revision); return false; }
            continue;
        }
        if (std::holds_alternative<RaidDroppedMagazineLocation>(asset.location))
        {
            if (definition->category != ItemCategory::Magazine)
            { receipt = failure(DomainErrorCode::IllegalDestination, "invalid dropped magazine", candidate.revision); return false; }
            continue;
        }
        const auto *stored =
            std::get_if<StoredAssetLocation>(&asset.location);
        if (stored == nullptr)
        {
            continue;
        }
        if(!profilePlacementWithinContainer(candidate,content,stored->container,stored->origin,*definition,asset.orientation))
        {
            receipt=failure(DomainErrorCode::IllegalDestination,"asset placement is invalid",candidate.revision);return false;
        }
        const auto footprint=inventoryFootprint(*definition,asset.orientation);
        auto &cells=occupiedCells[stored->container];
        for(int y=0;y<footprint.height;++y)
            for(int x=0;x<footprint.width;++x)
                if(!cells.emplace(stored->origin.x+x,stored->origin.y+y).second)
                {
                    receipt=failure(DomainErrorCode::IllegalDestination,"asset placement overlaps another asset",candidate.revision);return false;
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
                const bool hasContents=containersWithChildren.contains(id);
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
    if (!validateTacticalDevices(candidate, content).valid || !validateWeaponComponents(candidate, content).valid)
    {
        receipt = failure(DomainErrorCode::IllegalDestination, "invalid weapon components", candidate.revision);
        return false;
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
    for (const auto &[id, asset] : candidate.assets.records())
        if ((std::get_if<RaidReloadHeldMagazineLocation>(&asset.location) && std::get<RaidReloadHeldMagazineLocation>(asset.location).weaponAssetId==command.instanceId) ||
            (std::get_if<BaseReloadHeldMagazineLocation>(&asset.location) && std::get<BaseReloadHeldMagazineLocation>(asset.location).weaponAssetId==command.instanceId) ||
            (std::get_if<TrainingHeldMagazineLocation>(&asset.location) && std::get<TrainingHeldMagazineLocation>(asset.location).weaponAssetId==command.instanceId))
            return failure(DomainErrorCode::IllegalDestination, "RESOLVE RELOAD BEFORE MOVING THE WEAPON", candidate.revision);
    if (source && (std::holds_alternative<RaidReloadHeldMagazineLocation>(source->location) ||
                   std::holds_alternative<RaidDroppedMagazineLocation>(source->location) ||
                   std::holds_alternative<RetainedRaidMagazineLocation>(source->location) ||
                   std::holds_alternative<BaseReloadHeldMagazineLocation>(source->location) ||
                   std::holds_alternative<TrainingHeldMagazineLocation>(source->location) ||
                   std::holds_alternative<TrainingDroppedMagazineLocation>(source->location)))
        return failure(DomainErrorCode::IllegalDestination, "USE THE RAID MAGAZINE OPERATION", candidate.revision);
    if (source && (std::holds_alternative<InstalledWeaponComponentLocation>(source->location) ||
                   std::holds_alternative<InstalledHelmetDeviceLocation>(source->location) ||
                   std::holds_alternative<InstalledDeviceBatteryLocation>(source->location)))
        return failure(DomainErrorCode::IllegalDestination, "USE GUNSMITH TO REMOVE COMPONENTS", candidate.revision);
    if (source == nullptr)
    {
        return failure(
            DomainErrorCode::MissingAsset,
            "asset does not exist",
            candidate.revision);
    }
    if (std::holds_alternative<BaseServiceAssetLocation>(source->location))
    {
        return failure(
            DomainErrorCode::IllegalDestination,
            "asset is reserved by a Base service",
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
    const bool sourceInLegacyOverflow =
        assetIsLegacyOverflowAsset(candidate, command.instanceId);
    if (command.destination.container.kind == ProfileContainerKind::LegacyOverflow ||
        (candidate.pendingRaid && assetIsLegacyOverflowAsset(candidate, command.instanceId)) ||
        (command.destination.container.kind == ProfileContainerKind::AssetCompartment &&
         assetIsLegacyOverflowAsset(candidate, command.destination.container.ownerAssetId)))
        return failure(DomainErrorCode::IllegalDestination,
                       "legacy items must be withdrawn before use", candidate.revision);
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
        command.destination.origin.y > destinationSize.height - footprint.height ||
        (command.destination.container.kind == ProfileContainerKind::Stash &&
         candidate.stashLayoutVersion == 1 &&
         !stashFootprintFitsPage(command.destination.origin, footprint)))
    {
        return failure(
            DomainErrorCode::Capacity,
            "asset does not fit inside the destination",
            candidate.revision);
    }

    // Only unused batteries stack. Swapping or merging used cells would otherwise
    // silently copy the destination's charge or initialize a split to full charge.
    if (definition.battery)
    {
        const auto hits = overlappingAssets(candidate, content, command.destination, definition,
                                           command.destinationOrientation, source->instanceId);
        for (auto hit : hits)
        {
            const auto *other = candidate.assets.find(hit);
            if (other && other->definitionId == source->definitionId &&
                (source->remainingCharges != definition.maximumCharges || other->remainingCharges != definition.maximumCharges))
                return failure(DomainErrorCode::IllegalDestination, "USED BATTERIES CANNOT STACK", candidate.revision);
        }
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
    if ((sourceInIntake || sourceInLegacyOverflow) && !overlaps.empty())
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
                sourceInLegacyOverflow ? "legacy items must be withdrawn before use"
                    : "pending allocation cannot swap an asset back into intake",
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

    const auto detached = resolveRetainedRaidMagazinesInCandidate(
        candidate, content, command.instanceId, assetIsCarried(candidate, command.instanceId));
    if (!detached.valid) return failure(DomainErrorCode::Capacity, detached.message, candidate.revision);
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
    for (const auto &[id, asset] : candidate.assets.records())
        if ((std::get_if<RaidReloadHeldMagazineLocation>(&asset.location) && std::get<RaidReloadHeldMagazineLocation>(asset.location).weaponAssetId==command.instanceId) ||
            (std::get_if<BaseReloadHeldMagazineLocation>(&asset.location) && std::get<BaseReloadHeldMagazineLocation>(asset.location).weaponAssetId==command.instanceId) ||
            (std::get_if<TrainingHeldMagazineLocation>(&asset.location) && std::get<TrainingHeldMagazineLocation>(asset.location).weaponAssetId==command.instanceId))
            return failure(DomainErrorCode::IllegalDestination, "RESOLVE RELOAD BEFORE MOVING THE WEAPON", candidate.revision);
    if (source && (std::holds_alternative<RaidReloadHeldMagazineLocation>(source->location) ||
                   std::holds_alternative<RaidDroppedMagazineLocation>(source->location) ||
                   std::holds_alternative<RetainedRaidMagazineLocation>(source->location) ||
                   std::holds_alternative<BaseReloadHeldMagazineLocation>(source->location) ||
                   std::holds_alternative<TrainingHeldMagazineLocation>(source->location) ||
                   std::holds_alternative<TrainingDroppedMagazineLocation>(source->location)))
        return failure(DomainErrorCode::IllegalDestination, "USE THE RAID MAGAZINE OPERATION", candidate.revision);

    if (source == nullptr)
    {
        return failure(
            DomainErrorCode::MissingAsset,
            "asset does not exist",
            candidate.revision);
    }
    if (std::holds_alternative<BaseServiceAssetLocation>(source->location))
    {
        return failure(
            DomainErrorCode::IllegalDestination,
            "asset is reserved by a Base service",
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
        (stored->container == ProfileContainerId::baseIntake() ||
         assetIsLegacyOverflowAsset(candidate, command.instanceId)))
    {
        return failure(
            DomainErrorCode::IllegalDestination,
            assetIsLegacyOverflowAsset(candidate, command.instanceId)
                ? "legacy items must be withdrawn before use"
                : "pending allocation must be kept before it can be equipped",
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

    const auto detached = resolveRetainedRaidMagazinesInCandidate(
        candidate, content, command.instanceId, assetIsCarried(candidate, command.instanceId));
    if (!detached.valid) return failure(DomainErrorCode::Capacity, detached.message, candidate.revision);
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

ProfileValidationResult validateInventoryAssetGraph(
    const AssetRegistry &assets, const ContentRegistry &content,
    std::uint32_t stashLayoutVersion, std::uint32_t chestRigLayoutVersion)
{
    ProfileState candidate;
    candidate.stashLayoutVersion = stashLayoutVersion;
    candidate.chestRigLayoutVersion = chestRigLayoutVersion;
    candidate.assets = assets;
    InventoryReceipt receipt;
    const bool valid = validateCandidate(candidate, content, receipt, false);
    return {valid, receipt.message};
}

std::optional<AssetInstanceId> raidSceneContainerRoot(
    const ProfileState &profile, AssetInstanceId assetId)
{
    std::set<AssetInstanceId> visited;
    while (visited.insert(assetId).second)
    {
        const AssetRecord *asset = profile.assets.find(assetId);
        if (!asset) return std::nullopt;
        if (std::holds_alternative<RaidSceneContainerAssetLocation>(asset->location) ||
            std::holds_alternative<RaidCorpseContainerAssetLocation>(asset->location)) return assetId;
        if (const auto *stored = std::get_if<StoredAssetLocation>(&asset->location); stored && stored->container.kind == ProfileContainerKind::AssetCompartment)
            assetId = stored->container.ownerAssetId;
        else if (const auto *mag = std::get_if<InstalledMagazineLocation>(&asset->location)) assetId = mag->weaponAssetId;
        else if (const auto *part = std::get_if<InstalledWeaponComponentLocation>(&asset->location)) assetId = part->weaponAssetId;
        else if (const auto *retained = std::get_if<RetainedRaidMagazineLocation>(&asset->location)) assetId = retained->weaponAssetId;
        else if (const auto *battery = std::get_if<InstalledDeviceBatteryLocation>(&asset->location)) assetId = battery->deviceAssetId;
        else if (const auto *mount = std::get_if<InstalledHelmetDeviceLocation>(&asset->location)) assetId = mount->helmetAssetId;
        else return std::nullopt;
    }
    return std::nullopt;
}

bool raidSceneContainerObservable(const ProfileState &profile, const RaidSceneContainerAccess &access)
{
    const auto *asset = profile.assets.find(access.containerAssetId);
    if (!access.observation || !access.currentObservation ||
        !access.observation->matches(*access.currentObservation) ||
        !access.observation->matches(access.spaceId,access.playerCenter)) return false;
    if (asset && std::holds_alternative<RaidCorpseContainerAssetLocation>(asset->location))
    {
        const auto &corpse = std::get<RaidCorpseContainerAssetLocation>(asset->location);
        return corpse.deathPosition &&
            access.observation->boundsVisible({{corpse.deathPosition->x-26,corpse.deathPosition->y-10},{52,20}}) &&
            raidCorpseAccessible(profile, access.containerAssetId, access.spaceId, access.playerCenter);
    }
    const auto *scene = asset ? std::get_if<RaidSceneContainerAssetLocation>(&asset->location) : nullptr;
    if (!scene || !profile.pendingRaid || scene->raidId != profile.pendingRaid->raidId || scene->spaceId != access.spaceId ||
        !std::isfinite(access.playerCenter.x) || !std::isfinite(access.playerCenter.y)) return false;
    const float dx = scene->position.x - access.playerCenter.x, dy = scene->position.y - access.playerCenter.y;
    if (!access.observation->boundsVisible({{scene->position.x-24,scene->position.y-18},{48,36}})) return false;
    return dx * dx + dy * dy <= 72.0F * 72.0F;
}

bool raidSceneContainerLocked(const ProfileState &profile, AssetInstanceId containerAssetId) noexcept
{
    return profile.pendingRaid && profile.pendingRaid->sealedFreightCrate &&
        profile.pendingRaid->sealedFreightCrate->containerAssetId == containerAssetId &&
        !profile.pendingRaid->sealedFreightCrate->unlocked;
}

bool raidSceneContainerAccessible(const ProfileState &profile, const RaidSceneContainerAccess &access)
{
    return !raidSceneContainerLocked(profile, access.containerAssetId) &&
        raidSceneContainerObservable(profile, access);
}

bool raidFreightCrateKeyCarried(const ProfileState &profile)
{
    return std::any_of(profile.assets.records().begin(), profile.assets.records().end(),
        [&](const auto &entry) {
            return entry.second.definitionId == kFreightCrateKeyId && assetIsCarried(profile, entry.first);
        });
}

InventoryReceipt unlockRaidFreightCrate(
    ProfileState &profile,
    const RaidSceneContainerAccess &access,
    const CommandContext &context)
{
    if (context.transactionId.empty())
        return failure(DomainErrorCode::InvalidTransaction, "transaction ID must not be empty", profile.revision);

    auto *crate = profile.pendingRaid && profile.pendingRaid->sealedFreightCrate
        ? &*profile.pendingRaid->sealedFreightCrate : nullptr;
    if (!crate || crate->containerAssetId != access.containerAssetId)
        return failure(DomainErrorCode::IllegalDestination, "sealed freight crate is unavailable", profile.revision);
    if (profile.committedTransactions.contains(context.transactionId))
    {
        if (crate->unlocked)
            return {true, true, DomainErrorCode::None, {}, profile.revision};
        return failure(DomainErrorCode::InvalidTransaction, "transaction ID is already committed", profile.revision);
    }
    if (context.expectedRevision != profile.revision)
        return failure(DomainErrorCode::StaleRevision, "profile revision is stale", profile.revision);
    if (profile.revision == std::numeric_limits<ProfileRevision>::max())
        return failure(DomainErrorCode::RevisionOverflow, "profile revision cannot advance", profile.revision);
    if (crate->unlocked || !raidSceneContainerObservable(profile, access))
        return failure(DomainErrorCode::IllegalDestination, "sealed freight crate is unavailable", profile.revision);
    if (!raidFreightCrateKeyCarried(profile))
        return failure(DomainErrorCode::MissingAsset, "freight crate key must be carried", profile.revision);

    profile.committedTransactions.insert(context.transactionId);
    crate->unlocked = true;
    ++profile.revision;
    return {true, false, DomainErrorCode::None, {}, profile.revision};
}

namespace
{
bool sceneCommandAllowed(const ProfileState &profile, const InventoryCommand &command, const RaidSceneContainerAccess *access)
{
    const auto sourceId = std::visit([](const auto &value) { return value.instanceId; }, command);
    if (profile.pendingRaid && assetIsLegacyOverflowAsset(profile, sourceId)) return false;
    if (raidDroppedBackpackRoot(profile, sourceId)) return false;
    const auto sourceRoot = raidSceneContainerRoot(profile, sourceId);
    std::optional<AssetInstanceId> destinationRoot;
    if (const auto *move = std::get_if<InventoryMoveCommand>(&command);
        move && move->destination.container.kind == ProfileContainerKind::AssetCompartment)
    {
        const auto owner = move->destination.container.ownerAssetId;
        if (raidDroppedBackpackRoot(profile, owner)) return false;
        destinationRoot = raidSceneContainerRoot(profile, owner);
    }
    if (!sourceRoot && !destinationRoot) return true;
    if (!access || !raidSceneContainerAccessible(profile, *access)) return false;
    if ((sourceRoot && *sourceRoot != access->containerAssetId) || (destinationRoot && *destinationRoot != access->containerAssetId)) return false;
    const auto *source = profile.assets.find(sourceId);
    if (!source || std::holds_alternative<RaidSceneContainerAssetLocation>(source->location) ||
        std::holds_alternative<RaidCorpseContainerAssetLocation>(source->location)) return false;
    if (!sourceRoot && !assetIsCarried(profile, sourceId)) return false;
    if (const auto *move = std::get_if<InventoryMoveCommand>(&command); move && !destinationRoot)
        return move->destination.container.kind == ProfileContainerKind::AssetCompartment && assetIsCarried(profile, move->destination.container.ownerAssetId);
    return true;
}
}

InventoryPlan queryInventory(
    const ProfileState &profile,
    const ContentRegistry &content,
    const InventoryCommand &command,
    const RaidSceneContainerAccess *sceneAccess)
{
    if (!sceneCommandAllowed(profile, command, sceneAccess))
        return {false, DomainErrorCode::IllegalDestination, "Scene container is outside the accessible inventory scope", profile.revision};
    ProfileState candidate;
    candidate.stashLayoutVersion = profile.stashLayoutVersion;
    candidate.chestRigLayoutVersion = profile.chestRigLayoutVersion;
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
    const CommandContext &context,
    const RaidSceneContainerAccess *sceneAccess)
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

    if (!sceneCommandAllowed(profile, command, sceneAccess))
        return failure(DomainErrorCode::IllegalDestination, "Scene container is outside the accessible inventory scope", profile.revision);

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

    // Ground-origin rolls are already collected by pickupRaidLoot before they
    // can be stored. Audit frozen scene origins without allocating after apply.
    if (profile.pendingRaid)
        for (auto &loot : profile.pendingRaid->loot)
        {
            const auto *before = originalAssets.find(loot.assetId);
            const auto *after = profile.assets.find(loot.assetId);
            if ((loot.sceneContainerAssetId || loot.corpseContainerAssetId) && before &&
                (!after || before->location != after->location || before->quantity != after->quantity)) loot.collected = true;
        }
    profile.committedTransactions = std::move(committedTransactions);
    ++profile.revision;
    receipt.revision = profile.revision;
    return receipt;
}
