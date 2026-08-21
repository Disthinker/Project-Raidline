#include "profile_state.h"

#include <algorithm>
#include <cmath>
#include <bit>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <type_traits>

#include "alpha_content_ids.h"

namespace
{
constexpr InventoryGridSize kStashSize{20, 12};

bool validMedicalStatus(const MedicalStatusState &status) noexcept
{
    if (status.painkillerRemainingMs > 180000)
    {
        return false;
    }
    switch (status.bleeding)
    {
    case BleedingSeverity::None:
        return status.lightBleedingRemainingMs == 0 &&
               status.bleedingDamageRemainingMs == 0 &&
               status.painScreamRemainingMs == 0;
    case BleedingSeverity::Light:
        return status.lightBleedingRemainingMs > 0 &&
               status.lightBleedingRemainingMs <= 40000 &&
               status.bleedingDamageRemainingMs > 0 &&
               status.bleedingDamageRemainingMs <= 1000 &&
               status.painScreamRemainingMs > 0 &&
               status.painScreamRemainingMs <= 25000;
    case BleedingSeverity::Heavy:
        return status.lightBleedingRemainingMs == 0 &&
               status.bleedingDamageRemainingMs > 0 &&
               status.bleedingDamageRemainingMs <= 500 &&
               status.painScreamRemainingMs > 0 &&
               status.painScreamRemainingMs <= 25000;
    }
    return false;
}

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

void hashFloat(std::uint64_t &hash, float value) noexcept
{
    hashInteger(hash, std::bit_cast<std::uint32_t>(value));
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
        definition.armorProtection.has_value()
            ? definition.armorProtection->maximumDurability
            : definition.weaponCondition.has_value()
                ? definition.weaponCondition->maximumDurabilityCenti
                : 0U,
        definition.armorProtection.has_value()
            ? definition.armorProtection->maximumDurability
            : definition.weaponCondition.has_value()
                ? definition.weaponCondition->maximumDurabilityCenti
                : 0U,
        std::move(reliefBatchId),
        {},
        std::nullopt,
        WeaponMalfunctionType::None,
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
    placeNewAsset(profile, content, alpha_content::pistol);
    placeNewAsset(profile, content, alpha_content::chestRig);
    placeNewAsset(profile, content, alpha_content::backpack);
    for (int index = 0; index < 3; ++index)
    {
        placeNewAsset(profile, content, alpha_content::magazine);
    }
    for (int index = 0; index < 2; ++index)
    {
        placeNewAsset(profile, content, alpha_content::pistolMagazine);
    }
    placeNewAsset(profile, content, alpha_content::ammunition, 60);
    placeNewAsset(profile, content, alpha_content::ammunition, 30);
    for (int index = 0; index < 2; ++index)
    {
        placeNewAsset(profile, content, alpha_content::medkit);
    }
    placeNewAsset(profile, content, alpha_content::bandage);
    placeNewAsset(profile, content, alpha_content::tourniquet);
    placeNewAsset(profile, content, alpha_content::painkiller);
    placeNewAsset(profile, content, alpha_content::helmet);
    placeNewAsset(profile, content, alpha_content::bodyArmor);
    placeNewAsset(profile, content, alpha_content::weaponMaintenanceKit);
    placeNewAsset(profile, content, alpha_content::armorMaintenanceKit);

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

std::optional<AssetInstanceId> installedMagazine(
    const ProfileState &profile,
    AssetInstanceId weaponAssetId) noexcept
{
    for (const auto &[id, asset] : profile.assets.records())
    {
        const auto *installed =
            std::get_if<InstalledMagazineLocation>(&asset.location);
        if (installed != nullptr &&
            installed->weaponAssetId == weaponAssetId)
        {
            return id;
        }
    }
    return std::nullopt;
}

bool assetIsCarried(
    const ProfileState &profile,
    AssetInstanceId instanceId) noexcept
{
    std::set<AssetInstanceId> visited;
    AssetInstanceId current = instanceId;
    while (current != 0 && visited.insert(current).second)
    {
        const AssetRecord *asset = profile.assets.find(current);
        if (asset == nullptr)
        {
            return false;
        }
        if (std::holds_alternative<EquippedAssetLocation>(asset->location))
        {
            return true;
        }
        if (const auto *stored =
                std::get_if<StoredAssetLocation>(&asset->location))
        {
            if (stored->container.kind == ProfileContainerKind::Stash)
            {
                return false;
            }
            current = stored->container.ownerAssetId;
            continue;
        }
        if (const auto *installed =
                std::get_if<InstalledMagazineLocation>(&asset->location))
        {
            current = installed->weaponAssetId;
            continue;
        }
        return false;
    }
    return false;
}

std::vector<AssetInstanceId> carriedAssetIds(const ProfileState &profile)
{
    std::vector<AssetInstanceId> result;
    for (const auto &[id, asset] : profile.assets.records())
    {
        static_cast<void>(asset);
        if (assetIsCarried(profile, id))
        {
            result.push_back(id);
        }
    }
    return result;
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
        profile.assets.nextAssetId() == 0 ||
        profile.currentHealth < 0 || profile.currentHealth > 100 ||
        (profile.currentHealth == 0 && !profile.pendingRaid.has_value()))
    {
        return {false, "profile header is invalid"};
    }
    if (!validMedicalStatus(profile.medicalStatus))
    {
        return {false, "profile medical status is invalid"};
    }

    AssetInstanceId maximumId{};
    std::set<EquipmentSlotKind> occupiedSlots;
    std::set<AssetInstanceId> installedWeaponIds;
    std::set<AssetInstanceId> groundAssetIds;
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
        if (definition->armorProtection.has_value())
        {
            if (asset.currentMaximumDurability == 0 ||
                asset.currentMaximumDurability >
                    definition->armorProtection->maximumDurability ||
                asset.currentDurability > asset.currentMaximumDurability)
            {
                return {false, "armor durability is outside definition limits"};
            }
        }
        else if (definition->weaponCondition.has_value())
        {
            if (asset.currentMaximumDurability == 0 ||
                asset.currentMaximumDurability >
                    definition->weaponCondition->maximumDurabilityCenti ||
                asset.currentDurability > asset.currentMaximumDurability)
            {
                return {false, "weapon durability is outside definition limits"};
            }
        }
        else if (asset.currentMaximumDurability != 0 ||
                 asset.currentDurability != 0 ||
                 asset.weaponMalfunction != WeaponMalfunctionType::None)
        {
            return {false, "non-durable asset contains weapon state"};
        }
        if (asset.weaponMalfunction != WeaponMalfunctionType::None)
        {
            if (!definition->weaponCondition.has_value() ||
                asset.weaponMalfunction != WeaponMalfunctionType::Stovepipe)
            {
                return {false, "weapon malfunction is outside definition limits"};
            }
        }

        if (definition->category == ItemCategory::Magazine)
        {
            if (definition->magazineCapacity == 0 ||
                asset.magazineRounds.size() > definition->magazineCapacity ||
                !definition->compatibleAmmunitionDefinitionId.has_value())
            {
                return {false, "magazine state is invalid"};
            }
            for (const MagazineRoundRecord &round : asset.magazineRounds)
            {
                if (round.definitionId !=
                        *definition->compatibleAmmunitionDefinitionId ||
                    (round.reliefBatchId.has_value() &&
                     round.reliefBatchId->empty()))
                {
                    return {false, "magazine contains incompatible ammunition"};
                }
            }
        }
        else if (!asset.magazineRounds.empty())
        {
            return {false, "non-magazine asset contains rounds"};
        }

        if (asset.chamberedRound.has_value())
        {
            if (definition->category != ItemCategory::Weapon ||
                !definition->compatibleAmmunitionDefinitionId.has_value() ||
                asset.chamberedRound->definitionId !=
                    *definition->compatibleAmmunitionDefinitionId ||
                (asset.chamberedRound->reliefBatchId.has_value() &&
                 asset.chamberedRound->reliefBatchId->empty()))
            {
                return {false, "weapon chamber state is invalid"};
            }
        }

        if (const auto *equipped =
                std::get_if<EquippedAssetLocation>(&asset.location))
        {
            if (!itemCanEquipInSlot(*definition, equipped->slot) ||
                !occupiedSlots.insert(equipped->slot).second)
            {
                return {false, "equipment slot ownership is invalid"};
            }
            continue;
        }

        if (const auto *installed =
                std::get_if<InstalledMagazineLocation>(&asset.location))
        {
            const AssetRecord *weapon =
                profile.assets.find(installed->weaponAssetId);
            if (definition->category != ItemCategory::Magazine ||
                weapon == nullptr ||
                !installedWeaponIds.insert(installed->weaponAssetId).second)
            {
                return {false, "installed magazine ownership is invalid"};
            }
            const ItemDefinition *weaponDefinition{};
            try
            {
                weaponDefinition = &content.item(weapon->definitionId);
            }
            catch (...)
            {
                return {false, "installed magazine weapon is unknown"};
            }
            if (!weaponDefinition->compatibleMagazineDefinitionId.has_value() ||
                *weaponDefinition->compatibleMagazineDefinitionId !=
                    definition->definitionId)
            {
                return {false, "installed magazine is incompatible"};
            }
            continue;
        }

        if (const auto *ground =
                std::get_if<RaidGroundAssetLocation>(&asset.location))
        {
            if (!profile.pendingRaid.has_value() || ground->raidId.empty() ||
                ground->raidId != profile.pendingRaid->raidId)
            {
                return {false, "Raid ground ownership is invalid"};
            }
            groundAssetIds.insert(id);
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


    if (profile.pendingRaid.has_value())
    {
        const PendingRaidSnapshot &raid = *profile.pendingRaid;
        if (raid.raidId.empty() || raid.settlementId.empty() ||
            raid.rulesVersion.empty() || raid.mapDefinitionId.value().empty() ||
            raid.spawnExtractionPairId.empty() ||
            raid.enemyDeploymentId.value().empty() || raid.seed == 0 ||
            raid.startingHealth <= 0 || raid.startingHealth > 100 ||
            !validMedicalStatus(raid.startingMedicalStatus) ||
            raid.enemies.size() < 4 || raid.enemies.size() > 6 ||
            raid.loot.size() < 6 || raid.loot.size() > 9)
        {
            return {false, "pending Raid header is invalid"};
        }
        std::set<AssetInstanceId> snapshotLoot;
        for (const RaidLootSnapshot &loot : raid.loot)
        {
            const AssetRecord *asset = profile.assets.find(loot.assetId);
            if (loot.assetId == 0 ||
                !snapshotLoot.insert(loot.assetId).second ||
                (asset != nullptr &&
                 !groundAssetIds.contains(loot.assetId) &&
                 !assetIsCarried(profile, loot.assetId)))
            {
                return {false, "pending Raid Loot ownership is invalid"};
            }
        }
        for (AssetInstanceId groundAssetId : groundAssetIds)
        {
            if (!snapshotLoot.contains(groundAssetId))
            {
                return {false, "Raid ground asset is absent from snapshot"};
            }
        }
        if (!std::isfinite(raid.playerSpawn.x) ||
            !std::isfinite(raid.playerSpawn.y) ||
            !std::isfinite(raid.extractionPoint.position.x) ||
            !std::isfinite(raid.extractionPoint.position.y) ||
            !std::isfinite(raid.extractionPoint.size.x) ||
            !std::isfinite(raid.extractionPoint.size.y) ||
            raid.extractionPoint.size.x <= 0.0F ||
            raid.extractionPoint.size.y <= 0.0F)
        {
            return {false, "pending Raid geometry is invalid"};
        }
        for (const RaidEnemySnapshot &enemy : raid.enemies)
        {
            if (!std::isfinite(enemy.position.x) ||
                !std::isfinite(enemy.position.y) ||
                !std::isfinite(enemy.size.x) ||
                !std::isfinite(enemy.size.y) ||
                enemy.size.x <= 0.0F || enemy.size.y <= 0.0F ||
                enemy.maximumHealth <= 0)
            {
                return {false, "pending Raid enemy is invalid"};
            }
        }
        for (AssetInstanceId root : raid.carriedRootAssetIds)
        {
            const AssetRecord *asset = profile.assets.find(root);
            // The deployment snapshot records the roots that entered the
            // Raid, not permanent equipment-slot assignments. During a Raid
            // those roots may be moved between equipment and another carried
            // container, but must never leave the carried ownership tree.
            if (asset == nullptr || !assetIsCarried(profile, root))
            {
                return {false, "pending Raid carried root is invalid"};
            }
        }
    }
    else if (!groundAssetIds.empty())
    {
        return {false, "Raid ground asset exists without pending Raid"};
    }

    if (profile.lastRaidResult.has_value() &&
        profile.lastRaidResult->settlementId.empty())
    {
        return {false, "last Raid result is invalid"};
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
    hashInteger(hash, profile.currentHealth);
    hashInteger(hash, static_cast<std::uint32_t>(
        profile.medicalStatus.bleeding));
    hashInteger(hash, profile.medicalStatus.lightBleedingRemainingMs);
    hashInteger(hash, profile.medicalStatus.bleedingDamageRemainingMs);
    hashInteger(hash, profile.medicalStatus.painkillerRemainingMs);
    hashInteger(hash, profile.medicalStatus.painScreamRemainingMs);
    hashInteger(hash, profile.assets.nextAssetId());
    for (const auto &[id, asset] : profile.assets.records())
    {
        hashInteger(hash, id);
        hashBytes(hash, asset.definitionId.value());
        hashInteger(hash, asset.quantity);
        hashInteger(hash, static_cast<std::uint32_t>(asset.orientation));
        hashInteger(hash, asset.remainingCharges);
        hashInteger(hash, asset.currentMaximumDurability);
        hashInteger(hash, asset.currentDurability);
        hashInteger(hash, static_cast<std::uint32_t>(asset.weaponMalfunction));
        hashBytes(hash, asset.reliefBatchId.value_or(""));
        for (const MagazineRoundRecord &round : asset.magazineRounds)
        {
            hashBytes(hash, round.definitionId.value());
            hashBytes(hash, round.reliefBatchId.value_or(""));
        }
        hashBytes(hash, asset.chamberedRound.has_value()
            ? asset.chamberedRound->definitionId.value()
            : "");
        hashBytes(hash, asset.chamberedRound.has_value()
            ? asset.chamberedRound->reliefBatchId.value_or("")
            : "");
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
        else if (const auto *equipped =
                     std::get_if<EquippedAssetLocation>(&asset.location))
        {
            hashInteger(hash, 1U);
            hashInteger(hash, static_cast<std::uint32_t>(equipped->slot));
        }
        else if (const auto *installed =
                     std::get_if<InstalledMagazineLocation>(&asset.location))
        {
            hashInteger(hash, 2U);
            hashInteger(hash, installed->weaponAssetId);
        }
        else
        {
            const auto &ground = std::get<RaidGroundAssetLocation>(asset.location);
            hashInteger(hash, 3U);
            hashBytes(hash, ground.raidId);
            hashInteger(hash, ground.lootSlotIndex);
        }
    }
    for (const std::string &transaction : profile.committedTransactions)
    {
        hashBytes(hash, transaction);
    }
    for (const std::string &settlement : profile.committedSettlements)
    {
        hashBytes(hash, settlement);
    }
    if (profile.pendingRaid.has_value())
    {
        const PendingRaidSnapshot &raid = *profile.pendingRaid;
        hashBytes(hash, raid.raidId);
        hashBytes(hash, raid.settlementId);
        hashBytes(hash, raid.rulesVersion);
        hashBytes(hash, raid.mapDefinitionId.value());
        hashInteger(hash, raid.seed);
        hashBytes(hash, raid.spawnExtractionPairId);
        hashBytes(hash, raid.enemyDeploymentId.value());
        hashFloat(hash, raid.playerSpawn.x);
        hashFloat(hash, raid.playerSpawn.y);
        hashFloat(hash, raid.extractionPoint.position.x);
        hashFloat(hash, raid.extractionPoint.position.y);
        hashFloat(hash, raid.extractionPoint.size.x);
        hashFloat(hash, raid.extractionPoint.size.y);
        for (const RaidEnemySnapshot &enemy : raid.enemies)
        {
            hashFloat(hash, enemy.position.x);
            hashFloat(hash, enemy.position.y);
            hashFloat(hash, enemy.size.x);
            hashFloat(hash, enemy.size.y);
            hashInteger(hash, enemy.maximumHealth);
        }
        for (const RaidLootSnapshot &loot : raid.loot)
        {
            hashInteger(hash, loot.assetId);
            hashInteger(hash, loot.slotIndex);
            hashFloat(hash, loot.position.x);
            hashFloat(hash, loot.position.y);
        }
        for (AssetInstanceId root : raid.carriedRootAssetIds)
        {
            hashInteger(hash, root);
        }
        hashInteger(hash, raid.startingHealth);
        hashInteger(hash, static_cast<std::uint32_t>(
            raid.startingMedicalStatus.bleeding));
        hashInteger(hash, raid.startingMedicalStatus.lightBleedingRemainingMs);
        hashInteger(hash, raid.startingMedicalStatus.bleedingDamageRemainingMs);
        hashInteger(hash, raid.startingMedicalStatus.painkillerRemainingMs);
        hashInteger(hash, raid.startingMedicalStatus.painScreamRemainingMs);
    }
    if (profile.lastRaidResult.has_value())
    {
        hashBytes(hash, profile.lastRaidResult->settlementId);
        hashInteger(hash, static_cast<std::uint32_t>(
            profile.lastRaidResult->outcome));
        for (const ItemDefinitionId &id :
             profile.lastRaidResult->returnedItemDefinitionIds)
        {
            hashBytes(hash, id.value());
        }
        hashInteger(hash, profile.lastRaidResult->currencyDelta);
    }
    return hash;
}

WeaponReliabilityTier weaponReliabilityTier(
    const AssetRecord &weapon,
    const ItemDefinition &definition) noexcept
{
    if (!definition.weaponCondition.has_value() ||
        weapon.currentDurability == 0)
    {
        return WeaponReliabilityTier::Broken;
    }
    const std::uint32_t points = weapon.currentDurability / 100U;
    if (points >= 61U)
        return WeaponReliabilityTier::Reliable;
    if (points >= 31U)
        return WeaponReliabilityTier::Worn;
    if (points >= 11U)
        return WeaponReliabilityTier::HighRisk;
    return WeaponReliabilityTier::Critical;
}
