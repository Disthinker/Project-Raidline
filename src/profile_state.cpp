#include "profile_state.h"

#include <algorithm>
#include <limits>
#include <stdexcept>
#include <type_traits>

#include "alpha_content_ids.h"

namespace
{
constexpr InventoryGridSize kStashSize{20, 12};

bool overlaps(
    GridPosition leftOrigin,
    InventoryFootprint left,
    GridPosition rightOrigin,
    InventoryFootprint right) noexcept
{
    return leftOrigin.x < rightOrigin.x + right.width &&
           leftOrigin.x + left.width > rightOrigin.x &&
           leftOrigin.y < rightOrigin.y + right.height &&
           leftOrigin.y + left.height > rightOrigin.y;
}

bool pointInside(
    GridPosition point,
    GridPosition origin,
    InventoryFootprint footprint) noexcept
{
    return point.x >= origin.x && point.y >= origin.y &&
           point.x < origin.x + footprint.width &&
           point.y < origin.y + footprint.height;
}

void hashBytes(std::uint64_t &hash, std::string_view value) noexcept
{
    for (const unsigned char byte : value)
    {
        hash ^= byte;
        hash *= 1099511628211ULL;
    }
}

template <typename Integer>
void hashInteger(std::uint64_t &hash, Integer value) noexcept
{
    using Unsigned = std::make_unsigned_t<Integer>;
    Unsigned unsignedValue = static_cast<Unsigned>(value);
    for (std::size_t index = 0; index < sizeof(Integer); ++index)
    {
        hash ^= static_cast<unsigned char>(unsignedValue & 0xffU);
        hash *= 1099511628211ULL;
        unsignedValue >>= 8U;
    }
}

bool containerAccepts(
    const ProfileState &profile,
    const ContentRegistry &content,
    ProfileContainerId container,
    const ItemDefinition &item)
{
    if (container.kind == ProfileContainerKind::Stash)
    {
        return true;
    }

    const AssetRecord *owner = profile.assets.find(container.ownerAssetId);
    if (owner == nullptr)
    {
        return false;
    }
    const ItemDefinition &ownerDefinition =
        content.item(owner->definitionId);
    if (container.compartmentIndex >=
        ownerDefinition.containerCompartments.size())
    {
        return false;
    }

    const ContainerCompartmentDefinition &compartment =
        ownerDefinition.containerCompartments[container.compartmentIndex];
    return compartment.pocketKind == ContainerPocketKind::General ||
           item.category == ItemCategory::Magazine;
}

bool placementFits(
    const ProfileState &profile,
    const ContentRegistry &content,
    ProfileContainerId container,
    GridPosition origin,
    const ItemDefinition &definition,
    ItemOrientation orientation,
    std::optional<AssetInstanceId> ignoredAsset)
{
    InventoryGridSize size{};
    try
    {
        size = profileContainerSize(profile, content, container);
    }
    catch (...)
    {
        return false;
    }

    if (!containerAccepts(profile, content, container, definition))
    {
        return false;
    }

    const InventoryFootprint footprint =
        inventoryFootprint(definition, orientation);
    if (footprint.width <= 0 || footprint.height <= 0 ||
        origin.x < 0 || origin.y < 0 ||
        origin.x > size.width - footprint.width ||
        origin.y > size.height - footprint.height)
    {
        return false;
    }

    for (const AssetRecord *other : assetsInContainer(profile, container))
    {
        if (ignoredAsset.has_value() &&
            other->instanceId == *ignoredAsset)
        {
            continue;
        }
        const auto *stored = std::get_if<StoredAssetLocation>(&other->location);
        const ItemDefinition &otherDefinition =
            content.item(other->definitionId);
        if (overlaps(
                origin,
                footprint,
                stored->origin,
                inventoryFootprint(otherDefinition, other->orientation)))
        {
            return false;
        }
    }
    return true;
}

void placeNewAsset(
    ProfileState &profile,
    const ContentRegistry &content,
    const ItemDefinitionId &definitionId,
    std::uint32_t quantity = 1)
{
    const ItemDefinition &definition = content.item(definitionId);
    const auto origin = findFirstProfileFit(
        profile,
        content,
        ProfileContainerId::stash(),
        definition,
        ItemOrientation::Degrees0);
    if (!origin.has_value())
    {
        throw std::runtime_error{"new Alpha profile does not fit in Stash"};
    }
    static_cast<void>(profile.assets.create(
        definition,
        StoredAssetLocation{ProfileContainerId::stash(), *origin},
        quantity));
}
}

ProfileContainerId ProfileContainerId::stash() noexcept
{
    return ProfileContainerId{};
}

ProfileContainerId ProfileContainerId::compartment(
    AssetInstanceId ownerAssetId,
    std::uint32_t compartmentIndex) noexcept
{
    return ProfileContainerId{
        ProfileContainerKind::AssetCompartment,
        ownerAssetId,
        compartmentIndex};
}

AssetInstanceId AssetRegistry::nextAssetId() const noexcept
{
    return nextAssetId_;
}

void AssetRegistry::setNextAssetIdForLoad(AssetInstanceId nextAssetId)
{
    if (nextAssetId == 0)
    {
        throw std::invalid_argument{"next asset ID must be positive"};
    }
    nextAssetId_ = nextAssetId;
}

AssetInstanceId AssetRegistry::create(
    const ItemDefinition &definition,
    AssetLocation location,
    std::uint32_t quantity,
    std::optional<std::string> reliefBatchId)
{
    if (nextAssetId_ == 0 ||
        nextAssetId_ == std::numeric_limits<AssetInstanceId>::max() ||
        quantity == 0 || quantity > definition.maxStackSize)
    {
        throw std::invalid_argument{"invalid asset creation request"};
    }

    const AssetInstanceId id = nextAssetId_++;
    AssetRecord record{
        id,
        definition.definitionId,
        quantity,
        ItemOrientation::Degrees0,
        definition.maximumCharges,
        std::move(reliefBatchId),
        std::move(location)};
    if (!records_.emplace(id, std::move(record)).second)
    {
        throw std::logic_error{"asset ID allocation collided"};
    }
    return id;
}

bool AssetRegistry::insertLoaded(AssetRecord record)
{
    if (record.instanceId == 0)
    {
        return false;
    }
    return records_.emplace(record.instanceId, std::move(record)).second;
}

bool AssetRegistry::erase(AssetInstanceId instanceId) noexcept
{
    return records_.erase(instanceId) == 1U;
}

const AssetRecord *AssetRegistry::find(AssetInstanceId instanceId) const noexcept
{
    const auto found = records_.find(instanceId);
    return found == records_.end() ? nullptr : &found->second;
}

AssetRecord *AssetRegistry::findMutable(AssetInstanceId instanceId) noexcept
{
    const auto found = records_.find(instanceId);
    return found == records_.end() ? nullptr : &found->second;
}

const std::map<AssetInstanceId, AssetRecord> &
AssetRegistry::records() const noexcept
{
    return records_;
}

ProfileState makeNewAlphaProfile(
    std::string profileId,
    const ContentRegistry &content)
{
    if (profileId.empty())
    {
        throw std::invalid_argument{"profile ID must not be empty"};
    }

    ProfileState profile;
    profile.profileId = std::move(profileId);
    profile.currency = 200;

    placeNewAsset(profile, content, alpha_content::rifle);
    placeNewAsset(profile, content, alpha_content::chestRig);
    placeNewAsset(profile, content, alpha_content::backpack);
    for (int index = 0; index < 3; ++index)
    {
        placeNewAsset(profile, content, alpha_content::magazine);
    }
    placeNewAsset(profile, content, alpha_content::ammunition, 60);
    placeNewAsset(profile, content, alpha_content::ammunition, 30);
    for (int index = 0; index < 2; ++index)
    {
        placeNewAsset(profile, content, alpha_content::medkit);
    }

    const ProfileValidationResult validation =
        validateProfileState(profile, content);
    if (!validation.valid)
    {
        throw std::logic_error{validation.message};
    }
    return profile;
}

InventoryGridSize profileContainerSize(
    const ProfileState &profile,
    const ContentRegistry &content,
    ProfileContainerId container)
{
    if (container.kind == ProfileContainerKind::Stash)
    {
        return kStashSize;
    }
    const AssetRecord *owner = profile.assets.find(container.ownerAssetId);
    if (owner == nullptr)
    {
        throw std::out_of_range{"container owner does not exist"};
    }
    const ItemDefinition &definition = content.item(owner->definitionId);
    if (container.compartmentIndex >= definition.containerCompartments.size())
    {
        throw std::out_of_range{"container compartment does not exist"};
    }
    const auto &compartment =
        definition.containerCompartments[container.compartmentIndex];
    return InventoryGridSize{compartment.width, compartment.height};
}

std::vector<const AssetRecord *> assetsInContainer(
    const ProfileState &profile,
    ProfileContainerId container)
{
    std::vector<const AssetRecord *> result;
    for (const auto &[id, asset] : profile.assets.records())
    {
        static_cast<void>(id);
        const auto *stored = std::get_if<StoredAssetLocation>(&asset.location);
        if (stored != nullptr && stored->container == container)
        {
            result.push_back(&asset);
        }
    }
    return result;
}

std::optional<AssetInstanceId> equippedAsset(
    const ProfileState &profile,
    EquipmentSlotKind slot) noexcept
{
    for (const auto &[id, asset] : profile.assets.records())
    {
        const auto *equipped = std::get_if<EquippedAssetLocation>(&asset.location);
        if (equipped != nullptr && equipped->slot == slot)
        {
            return id;
        }
    }
    return std::nullopt;
}

std::optional<AssetInstanceId> profileAssetAtCell(
    const ProfileState &profile,
    const ContentRegistry &content,
    ProfileContainerId container,
    GridPosition cell) noexcept
{
    try
    {
        for (const AssetRecord *asset : assetsInContainer(profile, container))
        {
            const auto &location = std::get<StoredAssetLocation>(asset->location);
            if (pointInside(
                    cell,
                    location.origin,
                    inventoryFootprint(
                        content.item(asset->definitionId),
                        asset->orientation)))
            {
                return asset->instanceId;
            }
        }
    }
    catch (...)
    {
    }
    return std::nullopt;
}

std::optional<GridPosition> findFirstProfileFit(
    const ProfileState &profile,
    const ContentRegistry &content,
    ProfileContainerId container,
    const ItemDefinition &definition,
    ItemOrientation orientation,
    std::optional<AssetInstanceId> ignoredAsset) noexcept
{
    try
    {
        const InventoryGridSize size =
            profileContainerSize(profile, content, container);
        for (int y = 0; y < size.height; ++y)
        {
            for (int x = 0; x < size.width; ++x)
            {
                const GridPosition origin{x, y};
                if (placementFits(
                        profile,
                        content,
                        container,
                        origin,
                        definition,
                        orientation,
                        ignoredAsset))
                {
                    return origin;
                }
            }
        }
    }
    catch (...)
    {
    }
    return std::nullopt;
}

ProfileValidationResult validateProfileState(
    const ProfileState &profile,
    const ContentRegistry &content)
{
    if (profile.profileId.empty() || profile.revision == 0 ||
        profile.assets.nextAssetId() == 0)
    {
        return {false, "profile header is invalid"};
    }

    AssetInstanceId maximumId{};
    std::set<EquipmentSlotKind> occupiedSlots;
    for (const auto &[id, asset] : profile.assets.records())
    {
        if (id == 0 || id != asset.instanceId)
        {
            return {false, "asset ID is invalid"};
        }
        maximumId = std::max(maximumId, id);

        const ItemDefinition *definition{};
        try
        {
            definition = &content.item(asset.definitionId);
        }
        catch (...)
        {
            return {false, "asset definition is unknown"};
        }
        if (asset.quantity == 0 || asset.quantity > definition->maxStackSize ||
            !canUseItemOrientation(*definition, asset.orientation) ||
            asset.remainingCharges > definition->maximumCharges ||
            (definition->maximumCharges == 0 && asset.remainingCharges != 0))
        {
            return {false, "asset value is outside definition limits"};
        }

        if (const auto *equipped =
                std::get_if<EquippedAssetLocation>(&asset.location))
        {
            if (!definition->equipmentSlot.has_value() ||
                *definition->equipmentSlot != equipped->slot ||
                !occupiedSlots.insert(equipped->slot).second)
            {
                return {false, "equipment slot ownership is invalid"};
            }
            continue;
        }

        const auto &stored = std::get<StoredAssetLocation>(asset.location);
        if (!placementFits(
                profile,
                content,
                stored.container,
                stored.origin,
                *definition,
                asset.orientation,
                id))
        {
            return {false, "asset placement is invalid"};
        }

        if (stored.container.kind == ProfileContainerKind::AssetCompartment)
        {
            if (stored.container.ownerAssetId == id)
            {
                return {false, "container owns itself"};
            }
            if (!definition->containerCompartments.empty())
            {
                for (const auto &[childId, child] : profile.assets.records())
                {
                    static_cast<void>(childId);
                    const auto *childStored =
                        std::get_if<StoredAssetLocation>(&child.location);
                    if (childStored != nullptr &&
                        childStored->container.kind ==
                            ProfileContainerKind::AssetCompartment &&
                        childStored->container.ownerAssetId == id)
                    {
                        return {false, "non-empty container is nested"};
                    }
                }
            }
        }
    }

    if (profile.assets.nextAssetId() <= maximumId)
    {
        return {false, "asset high-water mark moved backward"};
    }
    return {true, {}};
}

std::uint64_t profileStateFingerprint(const ProfileState &profile) noexcept
{
    std::uint64_t hash = 1469598103934665603ULL;
    hashBytes(hash, profile.profileId);
    hashInteger(hash, profile.revision);
    hashInteger(hash, profile.currency);
    hashInteger(hash, static_cast<std::uint32_t>(profile.tutorial));
    hashInteger(hash, profile.assets.nextAssetId());
    for (const auto &[id, asset] : profile.assets.records())
    {
        hashInteger(hash, id);
        hashBytes(hash, asset.definitionId.value());
        hashInteger(hash, asset.quantity);
        hashInteger(hash, static_cast<std::uint32_t>(asset.orientation));
        hashInteger(hash, asset.remainingCharges);
        hashBytes(hash, asset.reliefBatchId.value_or(""));
        if (const auto *stored =
                std::get_if<StoredAssetLocation>(&asset.location))
        {
            hashInteger(hash, 0U);
            hashInteger(hash, static_cast<std::uint32_t>(stored->container.kind));
            hashInteger(hash, stored->container.ownerAssetId);
            hashInteger(hash, stored->container.compartmentIndex);
            hashInteger(hash, stored->origin.x);
            hashInteger(hash, stored->origin.y);
        }
        else
        {
            hashInteger(hash, 1U);
            hashInteger(hash, static_cast<std::uint32_t>(
                std::get<EquippedAssetLocation>(asset.location).slot));
        }
    }
    for (const std::string &transaction : profile.committedTransactions)
    {
        hashBytes(hash, transaction);
    }
    return hash;
}
