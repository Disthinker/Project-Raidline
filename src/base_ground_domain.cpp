#include "base_ground_domain.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <set>
#include <utility>

namespace
{
BaseGroundReceipt failure(
    DomainErrorCode error,
    std::string message,
    ProfileRevision revision)
{
    return BaseGroundReceipt{
        false, false, error, std::move(message), revision, 0};
}

bool finitePoint(Vec2 value) noexcept
{
    return std::isfinite(value.x) && std::isfinite(value.y) &&
        value.x >= 0.0F && value.y >= 0.0F;
}

bool accessMatchesActiveBase(
    const ProfileState &profile,
    const ContentRegistry &content,
    const BaseGroundAccess &access) noexcept
{
    if (access.baseSiteDefinitionId.value().empty() ||
        access.baseSiteDefinitionId !=
            profile.regionalOperations.technologyCore.baseSiteDefinitionId ||
        !finitePoint(access.playerCenter) ||
        !finitePoint(access.dropPosition) ||
        !std::isfinite(access.interactionRange) ||
        access.interactionRange <= 0.0F)
    {
        return false;
    }
    try
    {
        static_cast<void>(content.regionalBaseSite(
            access.baseSiteDefinitionId));
        const auto state = profile.regionalOperations.baseSites.find(
            access.baseSiteDefinitionId);
        return state != profile.regionalOperations.baseSites.end() &&
            state->second.unlocked;
    }
    catch (...)
    {
        return false;
    }
}

float distanceSquared(Vec2 left, Vec2 right) noexcept
{
    const float x = left.x - right.x;
    const float y = left.y - right.y;
    return x * x + y * y;
}

bool hasDirectChildren(
    const ProfileState &profile,
    AssetInstanceId ownerAssetId) noexcept
{
    return std::any_of(
        profile.assets.records().begin(),
        profile.assets.records().end(),
        [ownerAssetId](const auto &entry)
        {
            const auto *stored =
                std::get_if<StoredAssetLocation>(&entry.second.location);
            return stored != nullptr &&
                stored->container.kind ==
                    ProfileContainerKind::AssetCompartment &&
                stored->container.ownerAssetId == ownerAssetId;
        });
}

BaseGroundPlan containerAccessPlan(
    const ProfileState &profile,
    const ContentRegistry &content,
    AssetInstanceId containerAssetId,
    const BaseGroundAccess &access)
{
    if (profile.pendingRaid.has_value() ||
        !accessMatchesActiveBase(profile, content, access))
    {
        return BaseGroundPlan{
            false, DomainErrorCode::IllegalDestination,
            "Base ground access is not valid", profile.revision,
            containerAssetId};
    }
    const AssetRecord *container = profile.assets.find(containerAssetId);
    if (container == nullptr)
    {
        return BaseGroundPlan{
            false, DomainErrorCode::MissingAsset,
            "ground container does not exist", profile.revision,
            containerAssetId};
    }
    const auto *ground = std::get_if<BaseGroundAssetLocation>(
        &container->location);
    if (ground == nullptr ||
        ground->baseSiteDefinitionId != access.baseSiteDefinitionId ||
        distanceSquared(ground->position, access.playerCenter) >
            access.interactionRange * access.interactionRange)
    {
        return BaseGroundPlan{
            false, DomainErrorCode::IllegalDestination,
            "ground container is outside interaction range",
            profile.revision, containerAssetId};
    }
    const ItemDefinition &definition = content.item(container->definitionId);
    if (definition.containerCompartments.empty())
    {
        return BaseGroundPlan{
            false, DomainErrorCode::IllegalDestination,
            "ground asset is not a container", profile.revision,
            containerAssetId};
    }
    return BaseGroundPlan{
        true, DomainErrorCode::None, {}, profile.revision,
        containerAssetId};
}

bool assetIsInGroundContainer(
    const ProfileState &profile,
    AssetInstanceId assetId,
    AssetInstanceId containerAssetId) noexcept
{
    const AssetRecord *asset = profile.assets.find(assetId);
    const auto *stored = asset != nullptr
        ? std::get_if<StoredAssetLocation>(&asset->location)
        : nullptr;
    return stored != nullptr &&
        stored->container.kind == ProfileContainerKind::AssetCompartment &&
        stored->container.ownerAssetId == containerAssetId;
}

bool containerDestinationIsAccessible(
    const ProfileState &profile,
    ProfileContainerId destination,
    AssetInstanceId containerAssetId) noexcept
{
    return destination.kind == ProfileContainerKind::AssetCompartment &&
        (destination.ownerAssetId == containerAssetId ||
         assetIsCarried(profile, destination.ownerAssetId));
}

bool inventoryCommandUsesContainerScope(
    const ProfileState &profile,
    AssetInstanceId containerAssetId,
    const InventoryCommand &command) noexcept
{
    return std::visit(
        [&](const auto &typed)
        {
            const bool sourceAccessible = assetIsCarried(
                profile, typed.instanceId) ||
                assetIsInGroundContainer(
                    profile, typed.instanceId, containerAssetId);
            if (!sourceAccessible)
                return false;
            using Command = std::decay_t<decltype(typed)>;
            if constexpr (std::is_same_v<Command, InventoryMoveCommand>)
            {
                return containerDestinationIsAccessible(
                    profile, typed.destination.container,
                    containerAssetId);
            }
            else
            {
                return true;
            }
        },
        command);
}

std::vector<ProfileContainerId> carriedContainers(
    const ProfileState &profile,
    const ContentRegistry &content)
{
    std::vector<ProfileContainerId> result;
    constexpr std::array<EquipmentSlotKind, 2> slots{
        EquipmentSlotKind::ChestRig,
        EquipmentSlotKind::Backpack};
    for (const EquipmentSlotKind slot : slots)
    {
        const auto ownerId = equippedAsset(profile, slot);
        const AssetRecord *owner = ownerId.has_value()
            ? profile.assets.find(*ownerId)
            : nullptr;
        if (owner == nullptr)
        {
            continue;
        }
        const ItemDefinition &definition = content.item(owner->definitionId);
        for (std::uint32_t index{};
             index < definition.containerCompartments.size(); ++index)
        {
            result.push_back(ProfileContainerId::compartment(
                owner->instanceId, index));
        }
    }
    return result;
}

BaseGroundReceipt applyDrop(
    ProfileState &candidate,
    const ContentRegistry &content,
    const DropBaseGroundAssetCommand &command)
{
    if (candidate.pendingRaid.has_value() ||
        !accessMatchesActiveBase(candidate, content, command.access))
    {
        return failure(
            DomainErrorCode::IllegalDestination,
            "Base ground access is not valid",
            candidate.revision);
    }
    AssetRecord *asset = candidate.assets.findMutable(command.assetId);
    if (asset == nullptr)
    {
        return failure(
            DomainErrorCode::MissingAsset,
            "asset does not exist",
            candidate.revision);
    }
    const auto *stored = std::get_if<StoredAssetLocation>(&asset->location);
    const bool stashRoot = stored != nullptr &&
        stored->container == ProfileContainerId::stash();
    if (!assetIsCarried(candidate, command.assetId) &&
        !(stashRoot && command.access.stashAccessible))
    {
        return failure(
            DomainErrorCode::IllegalDestination,
            "asset is not accessible from the current Base position",
            candidate.revision);
    }
    if (std::holds_alternative<InstalledMagazineLocation>(asset->location))
    {
        return failure(
            DomainErrorCode::IllegalDestination,
            "installed magazine must be removed first",
            candidate.revision);
    }
    const ItemDefinition &definition = content.item(asset->definitionId);
    const std::uint32_t requested = command.quantity == 0
        ? asset->quantity
        : command.quantity;
    if (requested == 0 || requested > asset->quantity ||
        !canUseItemOrientation(definition, command.orientation))
    {
        return failure(
            DomainErrorCode::InvalidQuantity,
            "requested ground quantity or orientation is invalid",
            candidate.revision);
    }
    if (requested < asset->quantity)
    {
        if (definition.maxStackSize <= 1 ||
            hasDirectChildren(candidate, asset->instanceId))
        {
            return failure(
                DomainErrorCode::InvalidQuantity,
                "only a plain stack can be split onto Base ground",
                candidate.revision);
        }
        asset->quantity -= requested;
        const AssetInstanceId splitId = candidate.assets.create(
            definition,
            BaseGroundAssetLocation{
                command.access.baseSiteDefinitionId,
                command.access.dropPosition},
            requested,
            asset->reliefBatchId);
        AssetRecord *split = candidate.assets.findMutable(splitId);
        split->orientation = command.orientation;
        return BaseGroundReceipt{
            true, false, DomainErrorCode::None, {}, candidate.revision, splitId};
    }
    asset->location = BaseGroundAssetLocation{
        command.access.baseSiteDefinitionId,
        command.access.dropPosition};
    asset->orientation = command.orientation;
    return BaseGroundReceipt{
        true, false, DomainErrorCode::None, {}, candidate.revision,
        asset->instanceId};
}

BaseGroundReceipt applyPickup(
    ProfileState &candidate,
    const ContentRegistry &content,
    const PickupBaseGroundAssetCommand &command)
{
    if (candidate.pendingRaid.has_value() ||
        !accessMatchesActiveBase(candidate, content, command.access))
    {
        return failure(
            DomainErrorCode::IllegalDestination,
            "Base ground access is not valid",
            candidate.revision);
    }
    AssetRecord *asset = candidate.assets.findMutable(command.assetId);
    if (asset == nullptr)
    {
        return failure(
            DomainErrorCode::MissingAsset,
            "ground asset does not exist",
            candidate.revision);
    }
    const auto *ground = std::get_if<BaseGroundAssetLocation>(
        &asset->location);
    if (ground == nullptr ||
        ground->baseSiteDefinitionId != command.access.baseSiteDefinitionId ||
        distanceSquared(ground->position, command.access.playerCenter) >
            command.access.interactionRange * command.access.interactionRange)
    {
        return failure(
            DomainErrorCode::IllegalDestination,
            "ground asset is outside interaction range",
            candidate.revision);
    }
    const ItemDefinition &definition = content.item(asset->definitionId);
    const AssetInstanceId affectedId = asset->instanceId;
    if (definition.maxStackSize > 1)
    {
        std::optional<AssetInstanceId> mergeTarget;
        for (const auto &[id, target] : candidate.assets.records())
        {
            if (id == affectedId || target.definitionId != asset->definitionId ||
                target.reliefBatchId != asset->reliefBatchId ||
                !assetIsCarried(candidate, id) ||
                target.quantity > definition.maxStackSize - asset->quantity)
            {
                continue;
            }
            mergeTarget = id;
            break;
        }
        if (mergeTarget.has_value())
        {
            AssetRecord *target = candidate.assets.findMutable(*mergeTarget);
            const std::uint32_t quantity = asset->quantity;
            target->quantity += quantity;
            static_cast<void>(candidate.assets.erase(affectedId));
            return BaseGroundReceipt{
                true, false, DomainErrorCode::None, {}, candidate.revision,
                *mergeTarget};
        }
    }

    const bool nonEmptyContainer =
        !definition.containerCompartments.empty() &&
        hasDirectChildren(candidate, affectedId);
    if (!nonEmptyContainer)
    {
        for (const ProfileContainerId container :
             carriedContainers(candidate, content))
        {
            const auto origin = findFirstProfileFit(
                candidate,
                content,
                container,
                definition,
                asset->orientation,
                asset->instanceId);
            if (origin.has_value())
            {
                asset = candidate.assets.findMutable(affectedId);
                asset->location = StoredAssetLocation{container, *origin};
                return BaseGroundReceipt{
                    true, false, DomainErrorCode::None, {},
                    candidate.revision, affectedId};
            }
        }
    }
    for (const EquipmentSlotKind slot : itemEquipmentSlots(definition))
    {
        if (!equippedAsset(candidate, slot).has_value())
        {
            asset = candidate.assets.findMutable(affectedId);
            asset->location = EquippedAssetLocation{slot};
            return BaseGroundReceipt{
                true, false, DomainErrorCode::None, {},
                candidate.revision, affectedId};
        }
    }
    return failure(
        DomainErrorCode::Capacity,
        "no carried space or compatible empty equipment slot",
        candidate.revision);
}

BaseGroundReceipt apply(
    ProfileState &candidate,
    const ContentRegistry &content,
    const BaseGroundCommand &command)
{
    BaseGroundReceipt receipt = std::visit(
        [&candidate, &content](const auto &typed)
        {
            using Command = std::decay_t<decltype(typed)>;
            if constexpr (std::is_same_v<Command, DropBaseGroundAssetCommand>)
                return applyDrop(candidate, content, typed);
            else
                return applyPickup(candidate, content, typed);
        },
        command);
    if (!receipt.succeeded)
        return receipt;
    const ProfileValidationResult validation =
        validateProfileState(candidate, content);
    if (!validation.valid)
    {
        return failure(
            DomainErrorCode::InvalidProfile,
            validation.message,
            candidate.revision);
    }
    return receipt;
}
}

BaseGroundPlan queryBaseGround(
    const ProfileState &profile,
    const ContentRegistry &content,
    const BaseGroundCommand &command)
{
    ProfileState candidate = profile;
    const BaseGroundReceipt receipt = apply(candidate, content, command);
    return BaseGroundPlan{
        receipt.succeeded,
        receipt.error,
        receipt.message,
        profile.revision,
        receipt.affectedAssetId};
}

BaseGroundReceipt executeBaseGround(
    ProfileState &profile,
    const ContentRegistry &content,
    const BaseGroundCommand &command,
    const CommandContext &context)
{
    if (context.transactionId.empty())
        return failure(DomainErrorCode::InvalidTransaction,
                       "transaction ID must not be empty", profile.revision);
    if (profile.committedTransactions.contains(context.transactionId))
        return BaseGroundReceipt{true, true, DomainErrorCode::None, {},
                                 profile.revision, 0};
    if (context.expectedRevision != profile.revision)
        return failure(DomainErrorCode::StaleRevision,
                       "profile revision is stale", profile.revision);
    if (profile.revision == std::numeric_limits<ProfileRevision>::max())
        return failure(DomainErrorCode::RevisionOverflow,
                       "profile revision cannot advance", profile.revision);

    ProfileState candidate = profile;
    BaseGroundReceipt receipt = apply(candidate, content, command);
    if (!receipt.succeeded)
    {
        receipt.revision = profile.revision;
        return receipt;
    }
    candidate.committedTransactions.insert(context.transactionId);
    ++candidate.revision;
    receipt.revision = candidate.revision;
    profile = std::move(candidate);
    return receipt;
}

BaseGroundPlan queryBaseGroundContainerAccess(
    const ProfileState &profile,
    const ContentRegistry &content,
    AssetInstanceId containerAssetId,
    const BaseGroundAccess &access)
{
    return containerAccessPlan(
        profile, content, containerAssetId, access);
}

InventoryPlan queryBaseGroundContainerInventory(
    const ProfileState &profile,
    const ContentRegistry &content,
    AssetInstanceId containerAssetId,
    const BaseGroundAccess &access,
    const InventoryCommand &command)
{
    const BaseGroundPlan accessPlan = containerAccessPlan(
        profile, content, containerAssetId, access);
    if (!accessPlan.canCommit)
    {
        return InventoryPlan{
            false, accessPlan.error, accessPlan.message,
            profile.revision};
    }
    if (!inventoryCommandUsesContainerScope(
            profile, containerAssetId, command))
    {
        return InventoryPlan{
            false, DomainErrorCode::IllegalDestination,
            "inventory command is outside the open ground container scope",
            profile.revision};
    }
    return queryInventory(profile, content, command);
}

InventoryReceipt executeBaseGroundContainerInventory(
    ProfileState &profile,
    const ContentRegistry &content,
    AssetInstanceId containerAssetId,
    const BaseGroundAccess &access,
    const InventoryCommand &command,
    const CommandContext &context)
{
    if (profile.committedTransactions.contains(context.transactionId))
    {
        return InventoryReceipt{
            true, true, DomainErrorCode::None, {}, profile.revision};
    }
    if (context.expectedRevision != profile.revision)
    {
        return InventoryReceipt{
            false, false, DomainErrorCode::StaleRevision,
            "profile revision is stale", profile.revision};
    }
    const InventoryPlan plan = queryBaseGroundContainerInventory(
        profile, content, containerAssetId, access, command);
    if (!plan.canCommit)
    {
        return InventoryReceipt{
            false, false, plan.error, plan.message, profile.revision};
    }
    return executeInventory(profile, content, command, context);
}

std::vector<BaseGroundAssetProjection> projectBaseGroundAssets(
    const ProfileState &profile,
    const RegionalBaseSiteDefinitionId &siteDefinitionId)
{
    std::vector<BaseGroundAssetProjection> result;
    for (const auto &[id, asset] : profile.assets.records())
    {
        const auto *ground =
            std::get_if<BaseGroundAssetLocation>(&asset.location);
        if (ground == nullptr ||
            ground->baseSiteDefinitionId != siteDefinitionId)
            continue;
        result.push_back(BaseGroundAssetProjection{
            id, asset.definitionId, asset.quantity, asset.orientation,
            ground->position});
    }
    return result;
}

std::optional<BaseGroundAssetProjection> nearestBaseGroundAsset(
    const ProfileState &profile,
    const RegionalBaseSiteDefinitionId &siteDefinitionId,
    Vec2 point,
    float maximumDistance) noexcept
{
    std::optional<BaseGroundAssetProjection> result;
    float best = maximumDistance * maximumDistance;
    for (const auto &[id, asset] : profile.assets.records())
    {
        const auto *ground =
            std::get_if<BaseGroundAssetLocation>(&asset.location);
        if (ground == nullptr ||
            ground->baseSiteDefinitionId != siteDefinitionId)
            continue;
        const float distance = distanceSquared(point, ground->position);
        if (distance <= best)
        {
            best = distance;
            result = BaseGroundAssetProjection{
                id, asset.definitionId, asset.quantity, asset.orientation,
                ground->position};
        }
    }
    return result;
}
