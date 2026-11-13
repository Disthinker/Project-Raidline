#include "weapon_ammo_domain.h"

#include <algorithm>
#include <cmath>
#include <set>
#include <tuple>
#include <limits>
#include <map>
#include <type_traits>

namespace
{
WeaponAmmoReceipt failure(
    DomainErrorCode error,
    std::string message,
    ProfileRevision revision)
{
    return {false, false, error, std::move(message), revision,
            WeaponAmmoResult::Dry, std::nullopt};
}

bool mergeOrCreateAmmunition(
    ProfileState &candidate,
    const ContentRegistry &content,
    ProfileContainerId destination,
    const ItemDefinitionId &ammunitionId,
    std::optional<std::string> reliefBatchId,
    std::uint32_t quantity)
{
    if (destination.kind == ProfileContainerKind::LegacyOverflow)
        return false;
    const ItemDefinition &definition = content.item(ammunitionId);
    for (const AssetRecord *asset : assetsInContainer(candidate, destination))
    {
        if (asset->definitionId != ammunitionId ||
            asset->reliefBatchId != reliefBatchId ||
            asset->quantity >= definition.maxStackSize)
        {
            continue;
        }
        AssetRecord *target = candidate.assets.findMutable(asset->instanceId);
        const std::uint32_t transfer = std::min(
            quantity,
            definition.maxStackSize - target->quantity);
        target->quantity += transfer;
        quantity -= transfer;
        if (quantity == 0)
        {
            return true;
        }
    }
    while (quantity > 0)
    {
        const std::uint32_t stack = std::min(quantity, definition.maxStackSize);
        const auto origin = findFirstProfileFit(
            candidate,
            content,
            destination,
            definition,
            ItemOrientation::Degrees0);
        if (!origin.has_value())
        {
            return false;
        }
        static_cast<void>(candidate.assets.create(
            definition,
            StoredAssetLocation{destination, *origin},
            stack,
            reliefBatchId));
        quantity -= stack;
    }
    return true;
}

WeaponAmmoReceipt applyLoad(
    ProfileState &candidate,
    const ContentRegistry &content,
    const LoadMagazineCommand &command)
{
    AssetRecord *magazine = candidate.assets.findMutable(command.magazineAssetId);
    AssetRecord *ammunition = candidate.assets.findMutable(command.ammunitionAssetId);
    if (magazine == nullptr || ammunition == nullptr)
    {
        return failure(DomainErrorCode::MissingAsset,
                       "magazine or ammunition does not exist",
                       candidate.revision);
    }
    const ItemDefinition &magazineDefinition = content.item(magazine->definitionId);
    const ItemDefinition &ammunitionDefinition = content.item(ammunition->definitionId);
    if (magazineDefinition.category != ItemCategory::Magazine ||
        ammunitionDefinition.category != ItemCategory::Ammunition ||
        !content.ammunitionFitsMagazine(
            ammunition->definitionId, magazine->definitionId))
    {
        return failure(DomainErrorCode::IllegalDestination,
                       "ammunition is incompatible with magazine",
                       candidate.revision);
    }
    const std::uint32_t available = magazineDefinition.magazineCapacity -
        static_cast<std::uint32_t>(magazine->magazineRounds.size());
    const std::uint32_t requested = command.quantity == 0
        ? std::min(available, ammunition->quantity)
        : command.quantity;
    if (requested == 0 || requested > available ||
        requested > ammunition->quantity)
    {
        return failure(DomainErrorCode::InvalidQuantity,
                       "requested rounds do not fit",
                       candidate.revision);
    }
    const MagazineRoundRecord round{
        ammunition->definitionId,
        ammunition->reliefBatchId};
    magazine->magazineRounds.insert(
        magazine->magazineRounds.end(),
        requested,
        round);
    ammunition->quantity -= requested;
    if (ammunition->quantity == 0)
    {
        static_cast<void>(candidate.assets.erase(command.ammunitionAssetId));
    }
    return {true, false, DomainErrorCode::None, {}, candidate.revision,
            WeaponAmmoResult::Loaded, std::nullopt};
}

WeaponAmmoReceipt applyUnload(
    ProfileState &candidate,
    const ContentRegistry &content,
    const UnloadMagazineCommand &command)
{
    AssetRecord *magazine = candidate.assets.findMutable(command.magazineAssetId);
    if (magazine == nullptr)
    {
        return failure(DomainErrorCode::MissingAsset,
                       "magazine does not exist", candidate.revision);
    }
    const ItemDefinition &definition = content.item(magazine->definitionId);
    if (definition.category != ItemCategory::Magazine ||
        magazine->magazineRounds.empty())
    {
        return failure(DomainErrorCode::InvalidQuantity,
                       "magazine has no rounds to unload", candidate.revision);
    }
    using UnloadedRoundKey = std::pair<
        ItemDefinitionId,
        std::optional<std::string>>;
    std::map<UnloadedRoundKey, std::uint32_t> quantities;
    for (const MagazineRoundRecord &round : magazine->magazineRounds)
    {
        ++quantities[{round.definitionId, round.reliefBatchId}];
    }
    for (const auto &[key, quantity] : quantities)
    {
        if (!mergeOrCreateAmmunition(
                candidate,
                content,
                command.destination,
                key.first,
                key.second,
                quantity))
        {
            return failure(DomainErrorCode::Capacity,
                           "destination cannot hold unloaded ammunition",
                           candidate.revision);
        }
    }
    magazine = candidate.assets.findMutable(command.magazineAssetId);
    magazine->magazineRounds.clear();
    return {true, false, DomainErrorCode::None, {}, candidate.revision,
            WeaponAmmoResult::Unloaded, std::nullopt};
}

WeaponAmmoReceipt applyInstall(
    ProfileState &candidate,
    const ContentRegistry &content,
    const InstallMagazineCommand &command)
{
    AssetRecord *weapon = candidate.assets.findMutable(command.weaponAssetId);
    AssetRecord *magazine = candidate.assets.findMutable(command.magazineAssetId);
    if (weapon == nullptr || magazine == nullptr)
    {
        return failure(DomainErrorCode::MissingAsset,
                       "weapon or magazine does not exist", candidate.revision);
    }
    const ItemDefinition &weaponDefinition = content.item(weapon->definitionId);
    if (!content.magazineFitsWeapon(
            magazine->definitionId, weapon->definitionId))
    {
        return failure(DomainErrorCode::IllegalDestination,
                       "magazine is incompatible with weapon", candidate.revision);
    }
    const auto *stored = std::get_if<StoredAssetLocation>(&magazine->location);
    if (stored == nullptr)
    {
        return failure(DomainErrorCode::IllegalDestination,
                       "magazine must be stored before installation",
                       candidate.revision);
    }
    const StoredAssetLocation original = *stored;
    const auto current = installedMagazine(candidate, command.weaponAssetId);
    magazine->location = InstalledMagazineLocation{command.weaponAssetId};
    if (current.has_value())
    {
        AssetRecord *old = candidate.assets.findMutable(*current);
        old->location = original;
    }
    return {true, false, DomainErrorCode::None, {}, candidate.revision,
            WeaponAmmoResult::Installed, std::nullopt};
}

bool feedChamber(
    AssetRecord &weapon,
    AssetRecord *magazine)
{
    if (weapon.chamberedRound.has_value() || magazine == nullptr ||
        magazine->magazineRounds.empty())
    {
        return false;
    }
    weapon.chamberedRound = magazine->magazineRounds.front();
    magazine->magazineRounds.erase(magazine->magazineRounds.begin());
    return true;
}

WeaponAmmoReceipt applyInstallAndChamber(
    ProfileState &candidate,
    const ContentRegistry &content,
    const InstallMagazineAndChamberCommand &command)
{
    WeaponAmmoReceipt receipt = applyInstall(
        candidate,
        content,
        InstallMagazineCommand{
            command.weaponAssetId,
            command.magazineAssetId});
    if (!receipt.succeeded)
    {
        return receipt;
    }

    AssetRecord *weapon = candidate.assets.findMutable(command.weaponAssetId);
    AssetRecord *magazine = candidate.assets.findMutable(command.magazineAssetId);
    if (weapon == nullptr || magazine == nullptr)
    {
        return failure(DomainErrorCode::MissingAsset,
                       "weapon or magazine disappeared during installation",
                       candidate.revision);
    }
    if (feedChamber(*weapon, magazine))
    {
        receipt.result = WeaponAmmoResult::InstalledAndChambered;
    }
    return receipt;
}

WeaponAmmoReceipt applyUninstall(
    ProfileState &candidate,
    const ContentRegistry &content,
    const UninstallMagazineCommand &command)
{
    const auto installed = installedMagazine(candidate, command.weaponAssetId);
    if (!installed.has_value())
    {
        return failure(DomainErrorCode::MissingAsset,
                       "weapon has no installed magazine", candidate.revision);
    }
    AssetRecord *magazine = candidate.assets.findMutable(*installed);
    const ItemDefinition &definition = content.item(magazine->definitionId);
    if (!canUseItemOrientation(definition, command.destinationOrientation))
    {
        return failure(DomainErrorCode::IllegalDestination,
                       "requested orientation is not supported",
                       candidate.revision);
    }
    magazine->location = command.destination;
    magazine->orientation = command.destinationOrientation;
    if (!profilePlacementFits(
            candidate,
            content,
            command.destination.container,
            command.destination.origin,
            definition,
            command.destinationOrientation,
            magazine->instanceId))
    {
        return failure(DomainErrorCode::Capacity,
                       "magazine does not fit at destination",
                       candidate.revision);
    }
    return {true, false, DomainErrorCode::None, {}, candidate.revision,
            WeaponAmmoResult::Uninstalled, std::nullopt};
}

WeaponAmmoReceipt applyChamber(
    ProfileState &candidate,
    const ContentRegistry &content,
    const ChamberWeaponCommand &command)
{
    AssetRecord *weapon = candidate.assets.findMutable(command.weaponAssetId);
    if (weapon == nullptr)
    {
        return failure(DomainErrorCode::MissingAsset,
                       "weapon does not exist", candidate.revision);
    }
    const ItemDefinition &definition = content.item(weapon->definitionId);
    if (definition.category != ItemCategory::Weapon ||
        definition.compatibleMagazineDefinitionIds.empty())
    {
        return failure(DomainErrorCode::IllegalDestination,
                       "asset is not a magazine-fed weapon",
                       candidate.revision);
    }
    if (weapon->chamberedRound.has_value())
    {
        return failure(DomainErrorCode::InvalidQuantity,
                       "weapon is already chambered", candidate.revision);
    }
    const auto installed = installedMagazine(candidate, command.weaponAssetId);
    AssetRecord *magazine = installed.has_value()
        ? candidate.assets.findMutable(*installed)
        : nullptr;
    if (!feedChamber(*weapon, magazine))
    {
        return failure(DomainErrorCode::InvalidQuantity,
                       "no round is available to chamber", candidate.revision);
    }
    return {true, false, DomainErrorCode::None, {}, candidate.revision,
            WeaponAmmoResult::Chambered, std::nullopt};
}

bool weaponStoredInDroppedBackpack(const ProfileState &profile, const AssetRecord &weapon) noexcept
{
    const auto *stored = std::get_if<StoredAssetLocation>(&weapon.location);
    return stored && stored->container.kind == ProfileContainerKind::AssetCompartment &&
        raidDroppedBackpackRoot(profile, stored->container.ownerAssetId).has_value();
}

WeaponAmmoReceipt applyFire(
    ProfileState &candidate,
    const ContentRegistry &content,
    const FireWeaponCommand &command)
{
    if (assetIsLegacyOverflowAsset(candidate, command.weaponAssetId))
        return failure(DomainErrorCode::IllegalDestination,
            "legacy items must be withdrawn before use", candidate.revision);
    AssetRecord *weapon = candidate.assets.findMutable(command.weaponAssetId);
    if (weapon == nullptr)
    {
        return failure(DomainErrorCode::MissingAsset,
                       "weapon does not exist", candidate.revision);
    }
    if (weaponStoredInDroppedBackpack(candidate, *weapon))
        return failure(DomainErrorCode::IllegalDestination, "PICK UP BACKPACK FIRST", candidate.revision);
    const ItemDefinition &definition = content.item(weapon->definitionId);
    if (definition.category != ItemCategory::Weapon ||
        definition.compatibleMagazineDefinitionIds.empty() ||
        !queryWeaponFireMode(candidate, content, command.weaponAssetId))
    {
        return failure(DomainErrorCode::IllegalDestination,
                       "asset is not a magazine-fed weapon", candidate.revision);
    }
    if (definition.weaponCondition.has_value())
    {
        if (weapon->weaponMalfunction != WeaponMalfunctionType::None)
        {
            return {true, false, DomainErrorCode::None,
                    "weapon malfunction must be cleared", candidate.revision,
                    WeaponAmmoResult::BlockedByMalfunction, std::nullopt};
        }
        if (weapon->currentDurability == 0)
        {
            return {true, false, DomainErrorCode::None,
                    "weapon is broken", candidate.revision,
                    WeaponAmmoResult::Broken, std::nullopt};
        }
    }
    const auto installed = installedMagazine(candidate, command.weaponAssetId);
    AssetRecord *magazine = installed.has_value()
        ? candidate.assets.findMutable(*installed)
        : nullptr;
    const auto feed = [&weapon, &magazine]()
    {
        return feedChamber(*weapon, magazine);
    };

    if (!weapon->chamberedRound.has_value())
    {
        if (command.emptyChamberPolicy == EmptyChamberPolicy::RequireExplicitChamber &&
            magazine && !magazine->magazineRounds.empty())
            return {true, false, DomainErrorCode::None, "explicit chamber action required",
                candidate.revision, WeaponAmmoResult::NeedsChamber, std::nullopt};
        if (!feed())
        {
            return {true, false, DomainErrorCode::None,
                    "weapon is dry", candidate.revision,
                    WeaponAmmoResult::Dry, std::nullopt};
        }
        return {true, false, DomainErrorCode::None,
                "round chambered", candidate.revision,
                WeaponAmmoResult::Chambered, std::nullopt};
    }

    const ItemDefinitionId fired = weapon->chamberedRound->definitionId;
    weapon->chamberedRound.reset();

    bool malfunctioned = false;
    if (definition.weaponCondition.has_value())
    {
        const WeaponConditionDefinition &condition =
            *definition.weaponCondition;
        const WeaponReliabilityTier tier = weaponReliabilityTier(
            *weapon, definition);
        std::uint32_t baseChance{};
        switch (tier)
        {
        case WeaponReliabilityTier::Reliable:
            baseChance = 0;
            break;
        case WeaponReliabilityTier::Worn:
            baseChance = 50;
            break;
        case WeaponReliabilityTier::HighRisk:
            baseChance = 300;
            break;
        case WeaponReliabilityTier::Critical:
            baseChance = 1200;
            break;
        case WeaponReliabilityTier::Broken:
            baseChance = 10000;
            break;
        }
        const std::uint32_t chance = static_cast<std::uint32_t>(std::min(
            std::uint64_t{10000},
            (static_cast<std::uint64_t>(baseChance) *
             condition.reliabilityMultiplierBasisPoints) /
                10000U));
        if (!condition.malfunctionWeights.empty() &&
            command.malfunctionRollBasisPoints % 10000U < chance)
        {
            std::uint32_t totalWeight{};
            for (const WeaponMalfunctionWeight &entry :
                 condition.malfunctionWeights)
            {
                totalWeight += entry.weight;
            }
            std::uint32_t selected = totalWeight == 0
                ? 0U
                : command.malfunctionTypeRoll % totalWeight;
            for (const WeaponMalfunctionWeight &entry :
                 condition.malfunctionWeights)
            {
                if (selected < entry.weight)
                {
                    weapon->weaponMalfunction = entry.type;
                    malfunctioned = true;
                    break;
                }
                selected -= entry.weight;
            }
        }
        weapon->currentDurability =
            weapon->currentDurability > condition.wearPerSuccessfulShotCenti
                ? weapon->currentDurability -
                    condition.wearPerSuccessfulShotCenti
                : 0U;
    }
    if (!malfunctioned)
    {
        static_cast<void>(feed());
    }
    return {true, false, DomainErrorCode::None, {}, candidate.revision,
            malfunctioned
                ? WeaponAmmoResult::FiredAndMalfunctioned
                : WeaponAmmoResult::Fired,
            fired};
}

WeaponAmmoReceipt inspectFire(
    const ProfileState &profile,
    const ContentRegistry &content,
    const FireWeaponCommand &command)
{
    if (assetIsLegacyOverflowAsset(profile, command.weaponAssetId))
        return failure(DomainErrorCode::IllegalDestination,
            "legacy items must be withdrawn before use", profile.revision);
    const AssetRecord *weapon = profile.assets.find(command.weaponAssetId);
    if (weapon == nullptr)
    {
        return failure(DomainErrorCode::MissingAsset,
                       "weapon does not exist", profile.revision);
    }
    if (weaponStoredInDroppedBackpack(profile, *weapon))
        return failure(DomainErrorCode::IllegalDestination, "PICK UP BACKPACK FIRST", profile.revision);
    const ItemDefinition &definition = content.item(weapon->definitionId);
    if (definition.category != ItemCategory::Weapon ||
        definition.compatibleMagazineDefinitionIds.empty() ||
        !queryWeaponFireMode(profile, content, command.weaponAssetId))
    {
        return failure(DomainErrorCode::IllegalDestination,
                       "asset is not a magazine-fed weapon", profile.revision);
    }
    if (definition.weaponCondition.has_value())
    {
        if (weapon->weaponMalfunction != WeaponMalfunctionType::None)
        {
            return {true, false, DomainErrorCode::None,
                    "weapon malfunction must be cleared", profile.revision,
                    WeaponAmmoResult::BlockedByMalfunction, std::nullopt};
        }
        if (weapon->currentDurability == 0)
        {
            return {true, false, DomainErrorCode::None,
                    "weapon is broken", profile.revision,
                    WeaponAmmoResult::Broken, std::nullopt};
        }
    }
    if (weapon->chamberedRound.has_value())
    {
        return {true, false, DomainErrorCode::None, {}, profile.revision,
                WeaponAmmoResult::Fired,
                weapon->chamberedRound->definitionId};
    }
    const auto installed = installedMagazine(profile, command.weaponAssetId);
    const AssetRecord *magazine = installed.has_value()
        ? profile.assets.find(*installed)
        : nullptr;
    if (magazine != nullptr && !magazine->magazineRounds.empty())
    {
        return {true, false, DomainErrorCode::None,
                "round can be chambered", profile.revision,
                command.emptyChamberPolicy == EmptyChamberPolicy::RequireExplicitChamber
                    ? WeaponAmmoResult::NeedsChamber : WeaponAmmoResult::Chambered, std::nullopt};
    }
    return {true, false, DomainErrorCode::None,
            "weapon is dry", profile.revision,
            WeaponAmmoResult::Dry, std::nullopt};
}

bool validFireParticipants(
    const ProfileState &profile,
    const ContentRegistry &content,
    AssetInstanceId weaponAssetId,
    std::optional<AssetInstanceId> magazineAssetId) noexcept
{
    try
    {
        const AssetRecord *weapon = profile.assets.find(weaponAssetId);
        if (weapon == nullptr)
        {
            return false;
        }
        const ItemDefinition &weaponDefinition =
            content.item(weapon->definitionId);
        if (weaponDefinition.category != ItemCategory::Weapon ||
            weaponDefinition.compatibleMagazineDefinitionIds.empty() ||
            !queryWeaponFireMode(profile, content, weaponAssetId))
        {
            return false;
        }
        if (weaponDefinition.weaponCondition.has_value())
        {
            const WeaponConditionDefinition &condition =
                *weaponDefinition.weaponCondition;
            if (weapon->currentMaximumDurability == 0 ||
                weapon->currentMaximumDurability >
                    condition.maximumDurabilityCenti ||
                weapon->currentDurability > weapon->currentMaximumDurability ||
                (weapon->weaponMalfunction != WeaponMalfunctionType::None &&
                 weapon->weaponMalfunction != WeaponMalfunctionType::Stovepipe))
            {
                return false;
            }
        }
        else if (weapon->currentMaximumDurability != 0 ||
                 weapon->currentDurability != 0 ||
                 weapon->weaponMalfunction != WeaponMalfunctionType::None)
        {
            return false;
        }
        if (weapon->chamberedRound.has_value() &&
            (!content.ammunitionFitsWeapon(
                 weapon->chamberedRound->definitionId,
                 weapon->definitionId) ||
             (weapon->chamberedRound->reliefBatchId.has_value() &&
              weapon->chamberedRound->reliefBatchId->empty())))
        {
            return false;
        }

        const auto installed = installedMagazine(profile, weaponAssetId);
        if (installed != magazineAssetId)
        {
            return false;
        }
        if (!magazineAssetId.has_value())
        {
            return true;
        }
        const AssetRecord *magazine = profile.assets.find(*magazineAssetId);
        if (magazine == nullptr)
        {
            return false;
        }
        const auto *location =
            std::get_if<InstalledMagazineLocation>(&magazine->location);
        const ItemDefinition &magazineDefinition =
            content.item(magazine->definitionId);
        if (location == nullptr || location->weaponAssetId != weaponAssetId ||
            magazineDefinition.category != ItemCategory::Magazine ||
            !content.magazineFitsWeapon(
                magazine->definitionId, weapon->definitionId) ||
            magazine->magazineRounds.size() >
                magazineDefinition.magazineCapacity)
        {
            return false;
        }
        return std::all_of(
            magazine->magazineRounds.begin(),
            magazine->magazineRounds.end(),
            [&content, &magazine](const MagazineRoundRecord &round)
            {
                return content.ammunitionFitsMagazine(
                           round.definitionId, magazine->definitionId) &&
                    (!round.reliefBatchId.has_value() ||
                     !round.reliefBatchId->empty());
            });
    }
    catch (...)
    {
        return false;
    }
}

WeaponAmmoReceipt applyClearMalfunction(
    ProfileState &candidate,
    const ContentRegistry &content,
    const ClearWeaponMalfunctionCommand &command)
{
    AssetRecord *weapon = candidate.assets.findMutable(command.weaponAssetId);
    if (weapon == nullptr)
    {
        return failure(DomainErrorCode::MissingAsset,
                       "weapon does not exist", candidate.revision);
    }
    const ItemDefinition &definition = content.item(weapon->definitionId);
    if (!definition.weaponCondition.has_value() ||
        weapon->weaponMalfunction == WeaponMalfunctionType::None)
    {
        return failure(DomainErrorCode::InvalidQuantity,
                       "weapon has no malfunction", candidate.revision);
    }
    weapon->weaponMalfunction = WeaponMalfunctionType::None;
    const auto installed = installedMagazine(candidate, command.weaponAssetId);
    AssetRecord *magazine = installed.has_value()
        ? candidate.assets.findMutable(*installed)
        : nullptr;
    static_cast<void>(feedChamber(*weapon, magazine));
    return {true, false, DomainErrorCode::None, {}, candidate.revision,
            WeaponAmmoResult::MalfunctionCleared, std::nullopt};
}

WeaponAmmoReceipt inspectSetMode(const ProfileState &profile, const ContentRegistry &content,
                                 const SetWeaponFireModeCommand &command)
{
    const auto *weapon = profile.assets.find(command.weaponAssetId);
    if (!weapon) return failure(DomainErrorCode::MissingAsset, "weapon does not exist", profile.revision);
    const auto &definition = content.item(weapon->definitionId);
    if (!definition.weaponUse || !queryWeaponFireMode(profile, content, command.weaponAssetId) ||
        (profile.pendingRaid ? !assetIsCarried(profile, command.weaponAssetId) :
                              !assetIsBaseAccessible(profile, command.weaponAssetId)) ||
        std::find(definition.weaponUse->supportedFireModes.begin(),
            definition.weaponUse->supportedFireModes.end(), command.fireMode) ==
            definition.weaponUse->supportedFireModes.end())
        return failure(DomainErrorCode::IllegalDestination, "weapon fire mode is unavailable", profile.revision);
    return {true, false, DomainErrorCode::None, {}, profile.revision,
            WeaponAmmoResult::FireModeChanged, std::nullopt};
}

bool ammunitionCommandUsesDroppedBackpack(
    const ProfileState &profile, const WeaponAmmoCommand &command) noexcept
{
    if (!profile.pendingRaid || !profile.pendingRaid->backpackRecoveryEnabled)
        return false;

    const auto storedInDroppedPack = [&](AssetInstanceId id) {
        return raidDroppedBackpackRoot(profile, id).has_value();
    };
    const auto usesDestination = [&](ProfileContainerId destination) {
        return destination.kind == ProfileContainerKind::AssetCompartment &&
            storedInDroppedPack(destination.ownerAssetId);
    };
    return std::visit([&](const auto &typed) {
        bool blocked{};
        if constexpr (requires { typed.weaponAssetId; })
            blocked = blocked || storedInDroppedPack(typed.weaponAssetId);
        if constexpr (requires { typed.magazineAssetId; })
            blocked = blocked || storedInDroppedPack(typed.magazineAssetId);
        if constexpr (requires { typed.ammunitionAssetId; })
            blocked = blocked || storedInDroppedPack(typed.ammunitionAssetId);
        if constexpr (requires { typed.destination.kind; })
            blocked = blocked || usesDestination(typed.destination);
        if constexpr (requires { typed.destination.container; })
            blocked = blocked || usesDestination(typed.destination.container);
        return blocked;
    }, command);
}

WeaponAmmoReceipt apply(
    ProfileState &candidate,
    const ContentRegistry &content,
    const WeaponAmmoCommand &command)
{
    return std::visit(
        [&candidate, &content](const auto &typed)
        {
            using Command = std::decay_t<decltype(typed)>;
            const auto reserved = [&candidate](AssetInstanceId id) {
                const auto *asset = candidate.assets.find(id);
                return asset && (std::holds_alternative<RaidReloadHeldMagazineLocation>(asset->location) ||
                    std::holds_alternative<RaidDroppedMagazineLocation>(asset->location) ||
                    std::holds_alternative<RetainedRaidMagazineLocation>(asset->location) ||
                    std::holds_alternative<BaseReloadHeldMagazineLocation>(asset->location) ||
                    std::holds_alternative<TrainingHeldMagazineLocation>(asset->location) ||
                    std::holds_alternative<TrainingDroppedMagazineLocation>(asset->location));
            };
            if constexpr (requires { typed.magazineAssetId; })
                if (reserved(typed.magazineAssetId)) return failure(DomainErrorCode::IllegalDestination,
                    "magazine is reserved by a Raid operation", candidate.revision);
            bool legacy{};
            if constexpr (requires { typed.weaponAssetId; })
                legacy = legacy || assetIsLegacyOverflowAsset(candidate, typed.weaponAssetId);
            if constexpr (requires { typed.magazineAssetId; })
                legacy = legacy || assetIsLegacyOverflowAsset(candidate, typed.magazineAssetId);
            if constexpr (requires { typed.ammunitionAssetId; })
                legacy = legacy || assetIsLegacyOverflowAsset(candidate, typed.ammunitionAssetId);
            if constexpr (requires { typed.destination.kind; })
                legacy = legacy || typed.destination.kind == ProfileContainerKind::LegacyOverflow ||
                    (typed.destination.kind == ProfileContainerKind::AssetCompartment &&
                     assetIsLegacyOverflowAsset(candidate, typed.destination.ownerAssetId));
            if constexpr (requires { typed.destination.container; })
                legacy = legacy || typed.destination.container.kind == ProfileContainerKind::LegacyOverflow ||
                    (typed.destination.container.kind == ProfileContainerKind::AssetCompartment &&
                     assetIsLegacyOverflowAsset(candidate, typed.destination.container.ownerAssetId));
            if (legacy)
                return failure(DomainErrorCode::IllegalDestination,
                    "legacy items must be withdrawn before use", candidate.revision);
            if constexpr (std::is_same_v<Command, LoadMagazineCommand>)
                return applyLoad(candidate, content, typed);
            else if constexpr (std::is_same_v<Command, UnloadMagazineCommand>)
                return applyUnload(candidate, content, typed);
            else if constexpr (std::is_same_v<Command, InstallMagazineCommand>)
                return applyInstall(candidate, content, typed);
            else if constexpr (std::is_same_v<Command, InstallMagazineAndChamberCommand>)
                return applyInstallAndChamber(candidate, content, typed);
            else if constexpr (std::is_same_v<Command, UninstallMagazineCommand>)
                return applyUninstall(candidate, content, typed);
            else if constexpr (std::is_same_v<Command, ChamberWeaponCommand>)
                return applyChamber(candidate, content, typed);
            else if constexpr (std::is_same_v<Command, FireWeaponCommand>)
                return applyFire(candidate, content, typed);
            else if constexpr (std::is_same_v<Command, ClearWeaponMalfunctionCommand>)
                return applyClearMalfunction(candidate, content, typed);
            else
            {
                auto receipt = inspectSetMode(candidate, content, typed);
                if (receipt.succeeded) candidate.assets.findMutable(typed.weaponAssetId)->fireMode = typed.fireMode;
                return receipt;
            }
        },
        command);
}
}

WeaponAmmoPlan queryWeaponAmmo(
    const ProfileState &profile,
    const ContentRegistry &content,
    const WeaponAmmoCommand &command)
{
    if (ammunitionCommandUsesDroppedBackpack(profile, command))
        return {false, DomainErrorCode::IllegalDestination, "PICK UP BACKPACK FIRST",
                profile.revision, WeaponAmmoResult::Dry};
    if (const auto *mode = std::get_if<SetWeaponFireModeCommand>(&command))
    {
        const auto receipt = inspectSetMode(profile, content, *mode);
        return {receipt.succeeded, receipt.error, receipt.message, profile.revision, receipt.result};
    }
    ProfileState candidate;
    candidate.stashLayoutVersion = profile.stashLayoutVersion;
    candidate.chestRigLayoutVersion = profile.chestRigLayoutVersion;
    candidate.revision = profile.revision;
    candidate.assets = profile.assets;
    const WeaponAmmoReceipt receipt = apply(candidate, content, command);
    return WeaponAmmoPlan{
        receipt.succeeded,
        receipt.error,
        receipt.message,
        profile.revision,
        receipt.result};
}

WeaponAmmoReceipt executeWeaponAmmo(
    ProfileState &profile,
    const ContentRegistry &content,
    const WeaponAmmoCommand &command,
    const CommandContext &context)
{
    if (context.transactionId.empty())
    {
        return failure(DomainErrorCode::InvalidTransaction,
                       "transaction ID must not be empty", profile.revision);
    }
    if (profile.committedTransactions.contains(context.transactionId))
    {
        return {true, true, DomainErrorCode::None, {}, profile.revision,
                WeaponAmmoResult::Dry, std::nullopt};
    }
    if (context.expectedRevision != profile.revision)
    {
        return failure(DomainErrorCode::StaleRevision,
                       "profile revision is stale", profile.revision);
    }
    if (profile.revision == std::numeric_limits<ProfileRevision>::max())
    {
        return failure(DomainErrorCode::RevisionOverflow,
                       "profile revision cannot advance", profile.revision);
    }

    if (ammunitionCommandUsesDroppedBackpack(profile, command))
        return failure(DomainErrorCode::IllegalDestination, "PICK UP BACKPACK FIRST", profile.revision);
    if (const auto *mode = std::get_if<SetWeaponFireModeCommand>(&command))
    {
        auto receipt = inspectSetMode(profile, content, *mode);
        if (!receipt.succeeded) return receipt;
        profile.committedTransactions.insert(context.transactionId);
        profile.assets.findMutable(mode->weaponAssetId)->fireMode = mode->fireMode;
        ++profile.revision;
        receipt.revision = profile.revision;
        return receipt;
    }
    ProfileState candidate = profile;
    WeaponAmmoReceipt receipt = apply(candidate, content, command);
    if (!receipt.succeeded)
    {
        return receipt;
    }
    if (receipt.result == WeaponAmmoResult::Dry ||
        receipt.result == WeaponAmmoResult::NeedsChamber ||
        receipt.result == WeaponAmmoResult::BlockedByMalfunction ||
        receipt.result == WeaponAmmoResult::Broken)
    {
        receipt.revision = profile.revision;
        return receipt;
    }
    candidate.committedTransactions.insert(context.transactionId);
    ++candidate.revision;
    const ProfileValidationResult validation =
        validateProfileState(candidate, content);
    if (!validation.valid)
    {
        return failure(DomainErrorCode::InvalidProfile,
                       validation.message, profile.revision);
    }
    profile = std::move(candidate);
    receipt.revision = profile.revision;
    return receipt;
}

WeaponAmmoPlan queryFireWeapon(
    const ProfileState &profile,
    const ContentRegistry &content,
    const FireWeaponCommand &command)
{
    const WeaponAmmoReceipt receipt = inspectFire(profile, content, command);
    return WeaponAmmoPlan{
        receipt.succeeded,
        receipt.error,
        receipt.message,
        profile.revision,
        receipt.result};
}

WeaponAmmoReceipt executeFireWeapon(
    ProfileState &profile,
    const ContentRegistry &content,
    const FireWeaponCommand &command,
    const CommandContext &context)
{
    if (context.transactionId.empty())
    {
        return failure(DomainErrorCode::InvalidTransaction,
                       "transaction ID must not be empty", profile.revision);
    }
    if (profile.committedTransactions.contains(context.transactionId))
    {
        return {true, true, DomainErrorCode::None, {}, profile.revision,
                WeaponAmmoResult::Dry, std::nullopt};
    }
    if (context.expectedRevision != profile.revision)
    {
        return failure(DomainErrorCode::StaleRevision,
                       "profile revision is stale", profile.revision);
    }
    if (profile.revision == std::numeric_limits<ProfileRevision>::max())
    {
        return failure(DomainErrorCode::RevisionOverflow,
                       "profile revision cannot advance", profile.revision);
    }

    const AssetRecord *weapon = profile.assets.find(command.weaponAssetId);
    if (weapon == nullptr)
    {
        return failure(DomainErrorCode::MissingAsset,
                       "weapon does not exist", profile.revision);
    }
    const std::optional<AssetInstanceId> magazineId =
        installedMagazine(profile, command.weaponAssetId);
    const AssetRecord weaponBefore = *weapon;
    const AssetRecord *magazine = magazineId.has_value()
        ? profile.assets.find(*magazineId)
        : nullptr;
    if (magazineId.has_value() && magazine == nullptr)
    {
        return failure(DomainErrorCode::InvalidProfile,
                       "installed magazine does not exist", profile.revision);
    }
    const std::optional<AssetRecord> magazineBefore = magazine != nullptr
        ? std::optional<AssetRecord>{*magazine}
        : std::nullopt;

    WeaponAmmoReceipt receipt = applyFire(profile, content, command);
    if (!receipt.succeeded || receipt.result == WeaponAmmoResult::Dry ||
        receipt.result == WeaponAmmoResult::NeedsChamber ||
        receipt.result == WeaponAmmoResult::BlockedByMalfunction ||
        receipt.result == WeaponAmmoResult::Broken)
    {
        receipt.revision = profile.revision;
        return receipt;
    }
    if (!validFireParticipants(
            profile, content, command.weaponAssetId, magazineId))
    {
        *profile.assets.findMutable(command.weaponAssetId) = weaponBefore;
        if (magazineId.has_value() && magazineBefore.has_value())
        {
            *profile.assets.findMutable(*magazineId) = *magazineBefore;
        }
        return failure(DomainErrorCode::InvalidProfile,
                       "weapon fire produced invalid participant state",
                       profile.revision);
    }

    profile.committedTransactions.insert(context.transactionId);
    ++profile.revision;
    receipt.revision = profile.revision;
    return receipt;
}

std::size_t magazineRoundCount(
    const ProfileState &profile,
    AssetInstanceId magazineAssetId) noexcept
{
    const AssetRecord *magazine = profile.assets.find(magazineAssetId);
    return magazine == nullptr ? 0U : magazine->magazineRounds.size();
}

std::optional<WeaponFireMode> queryWeaponFireMode(
    const ProfileState &profile, const ContentRegistry &content, AssetInstanceId id) noexcept
{
    try
    {
        const auto *asset = profile.assets.find(id);
        if (!asset || !asset->fireMode) return std::nullopt;
        const auto &definition = content.item(asset->definitionId);
        if (!definition.weaponUse) return std::nullopt;
        const auto &modes = definition.weaponUse->supportedFireModes;
        return std::find(modes.begin(), modes.end(), *asset->fireMode) != modes.end()
            ? asset->fireMode : std::nullopt;
    }
    catch (...) { return std::nullopt; }
}

namespace
{
bool knownReloadRole(RaidReloadMagazineRole role)
{
    return role == RaidReloadMagazineRole::NewMagazine || role == RaidReloadMagazineRole::OldMagazine;
}

bool validRaidSpace(const ProfileState &facts, const ContentRegistry &content,
                   const RaidSpaceDefinitionId &space, std::optional<Vec2> position = std::nullopt)
{
    if (!facts.pendingRaid) return false;
    const auto &map = content.map(facts.pendingRaid->mapDefinitionId);
    Vec2 size = map.worldSize;
    if (space != outdoorRaidSpaceId())
    {
        const auto found = std::find_if(map.interiors.begin(), map.interiors.end(),
            [&](const auto &interior) { return interior.id == space; });
        if (found == map.interiors.end()) return false;
        size = found->worldSize;
    }
    return !position || (std::isfinite(position->x) && std::isfinite(position->y) &&
        position->x >= 0 && position->y >= 0 && position->x <= size.x && position->y <= size.y);
}

ProfileState magazineScratch(const ProfileState &profile)
{
    ProfileState candidate;
    candidate.revision = profile.revision;
    candidate.stashLayoutVersion = profile.stashLayoutVersion;
    candidate.chestRigLayoutVersion = profile.chestRigLayoutVersion;
    candidate.assets = profile.assets;
    candidate.baseReloadRecovery = profile.baseReloadRecovery;
    return candidate;
}

bool isChestRigStored(const ProfileState &profile, const StoredAssetLocation &location)
{
    return location.container.kind == ProfileContainerKind::AssetCompartment &&
        equippedAsset(profile, EquipmentSlotKind::ChestRig) == location.container.ownerAssetId;
}

std::vector<ProfileContainerId> magazineContainers(const ProfileState &profile,
    const ContentRegistry &content, bool chestOnly)
{
    std::vector<ProfileContainerId> result;
    for (const auto slot : {EquipmentSlotKind::Backpack, EquipmentSlotKind::ChestRig})
    {
        if (chestOnly && slot != EquipmentSlotKind::ChestRig) continue;
        const auto owner = equippedAsset(profile, slot);
        if (!owner) continue;
        const auto *asset = profile.assets.find(*owner);
        const auto &definition = content.item(asset->definitionId);
        for (std::size_t i = 0; i < definition.containerCompartments.size(); ++i)
            result.push_back(ProfileContainerId::compartment(*owner, static_cast<std::uint32_t>(i)));
    }
    return result;
}

std::optional<RaidMagazinePlacement> magazineReturnPlacement(
    const ProfileState &profile, const ContentRegistry &content, const AssetRecord &asset,
    bool raid, bool chestOnly, std::optional<RaidMagazinePlacement> preferred = std::nullopt,
    bool allowStash = false)
{
    const auto &definition = content.item(asset.definitionId);
    const auto fits = [&](const RaidMagazinePlacement &placement) {
        return canUseItemOrientation(definition, placement.orientation) && profilePlacementFits(
            profile, content, placement.location.container, placement.location.origin,
            definition, placement.orientation, asset.instanceId);
    };
    std::vector<ProfileContainerId> containers = raid ? magazineContainers(profile, content, chestOnly)
        : std::vector<ProfileContainerId>{ProfileContainerId::stash()};
    if(raid && allowStash)containers.push_back(ProfileContainerId::stash());
    if (preferred && std::find(containers.begin(), containers.end(), preferred->location.container) != containers.end() &&
        fits(*preferred)) return preferred;
    for (const auto container : containers)
        if (const auto origin = findFirstProfileFit(profile, content, container, definition,
                                                    asset.orientation, asset.instanceId))
            return RaidMagazinePlacement{StoredAssetLocation{container, *origin}, asset.orientation};
    return std::nullopt;
}

bool validMagazine(const AssetRecord &asset, const ContentRegistry &content,
                   const ItemDefinitionId &weaponDefinition)
{
    const auto &definition = content.item(asset.definitionId);
    if (definition.category != ItemCategory::Magazine || asset.quantity != 1 ||
        asset.magazineRounds.size() > definition.magazineCapacity ||
        !content.magazineFitsWeapon(asset.definitionId, weaponDefinition)) return false;
    return std::all_of(asset.magazineRounds.begin(), asset.magazineRounds.end(), [&](const auto &round) {
        return content.ammunitionFitsMagazine(round.definitionId, asset.definitionId) &&
            (!round.reliefBatchId || !round.reliefBatchId->empty());
    });
}

WeaponActionScope actionScope(const RaidReloadBinding &binding)
{
    return binding.activity.value_or(WeaponActionScope{WeaponActionActivity::Raid, {}, binding.raidId});
}
bool baseActivity(const WeaponActionScope &scope)
{
    return scope.activity == WeaponActionActivity::BaseDaily || scope.activity == WeaponActionActivity::BaseDefense;
}
bool validActivity(const ProfileState &profile, const WeaponActionScope &scope)
{
    if (scope.activityId.empty()) return false;
    if (scope.activity == WeaponActionActivity::Raid)
        return profile.pendingRaid && profile.pendingRaid->raidId == scope.activityId && scope.baseSiteDefinitionId.value().empty();
    if (scope.activity == WeaponActionActivity::Training)
        return !profile.pendingRaid && !profile.activeBaseDefense && scope.baseSiteDefinitionId.value().empty();
    const auto site = profile.homeFounding.established
        ? profile.regionalOperations.technologyCore.baseSiteDefinitionId : kFoundingRegion;
    if (profile.pendingRaid || scope.baseSiteDefinitionId != site) return false;
    if (scope.activity == WeaponActionActivity::BaseDaily)
        return !profile.activeBaseDefense && scope.activityId == site.value();
    return scope.activity == WeaponActionActivity::BaseDefense && profile.activeBaseDefense &&
        profile.activeBaseDefense->eventId == scope.activityId && profile.activeBaseDefense->siteDefinitionId == site.value();
}
bool boundedPoint(Vec2 point, Vec2 size)
{
    return std::isfinite(size.x) && std::isfinite(size.y) && size.x > 0 && size.y > 0 &&
        std::isfinite(point.x) && std::isfinite(point.y) && point.x >= 0 && point.y >= 0 && point.x <= size.x && point.y <= size.y;
}
bool anyHeldByWeapon(const AssetRecord &asset, AssetInstanceId weapon)
{
    if (const auto *h = std::get_if<RaidReloadHeldMagazineLocation>(&asset.location)) return h->weaponAssetId == weapon;
    if (const auto *h = std::get_if<BaseReloadHeldMagazineLocation>(&asset.location)) return h->weaponAssetId == weapon;
    if (const auto *h = std::get_if<TrainingHeldMagazineLocation>(&asset.location)) return h->weaponAssetId == weapon;
    return false;
}
bool heldBy(const AssetRecord *asset, const RaidReloadBinding &binding, RaidReloadMagazineRole role)
{
    if (!asset) return false;
    const auto scope = actionScope(binding);
    if (scope.activity == WeaponActionActivity::Raid)
    {
        const auto *h = std::get_if<RaidReloadHeldMagazineLocation>(&asset->location);
        return h && h->raidId == binding.raidId && h->spaceId == binding.spaceId &&
            h->weaponAssetId == binding.weaponAssetId && h->actionId == binding.actionId && h->role == role;
    }
    if (baseActivity(scope))
    {
        const auto *h = std::get_if<BaseReloadHeldMagazineLocation>(&asset->location);
        return h && h->activity == scope && h->weaponAssetId == binding.weaponAssetId && h->actionId == binding.actionId && h->role == role;
    }
    const auto *h = std::get_if<TrainingHeldMagazineLocation>(&asset->location);
    return h && h->sessionId == scope.activityId && h->weaponAssetId == binding.weaponAssetId && h->actionId == binding.actionId && h->role == role;
}
AssetLocation heldLocation(const RaidReloadBinding &binding, RaidReloadMagazineRole role)
{
    const auto scope = actionScope(binding);
    if (scope.activity == WeaponActionActivity::Raid)
        return RaidReloadHeldMagazineLocation{binding.raidId,binding.spaceId,binding.weaponAssetId,binding.actionId,role};
    if (baseActivity(scope)) return BaseReloadHeldMagazineLocation{scope,binding.weaponAssetId,binding.actionId,role};
    return TrainingHeldMagazineLocation{scope.activityId,binding.weaponAssetId,binding.actionId,role};
}
bool validBinding(const ProfileState &candidate, const ProfileState &facts,
                  const ContentRegistry &content, const RaidReloadBinding &binding)
{
    const auto scope = actionScope(binding);
    const auto *weapon = candidate.assets.find(binding.weaponAssetId);
    const auto *fresh = candidate.assets.find(binding.newMagazineAssetId);
    const auto *old = binding.oldMagazineAssetId ? candidate.assets.find(*binding.oldMagazineAssetId) : nullptr;
    return validActivity(facts, scope) &&
        (!baseActivity(scope) || !facts.baseReloadRecovery ||
            (facts.baseReloadRecovery->actionId==binding.actionId && facts.baseReloadRecovery->activity==scope &&
             facts.baseReloadRecovery->weaponAssetId==binding.weaponAssetId)) &&
        (scope.activity != WeaponActionActivity::Raid || (binding.raidId == scope.activityId && validRaidSpace(facts,content,binding.spaceId))) &&
        !binding.actionId.empty() && binding.weaponAssetId != 0 && binding.newMagazineAssetId != 0 &&
        binding.newMagazineAssetId != binding.weaponAssetId &&
        (!binding.oldMagazineAssetId || (*binding.oldMagazineAssetId != binding.newMagazineAssetId &&
                                       *binding.oldMagazineAssetId != binding.weaponAssetId)) && weapon && fresh &&
        validMagazine(*fresh,content,weapon->definitionId) &&
        (!binding.oldMagazineAssetId || (old && validMagazine(*old,content,weapon->definitionId))) &&
        std::holds_alternative<EquippedAssetLocation>(weapon->location) && queryWeaponFireMode(candidate,content,binding.weaponAssetId).has_value();
}
bool validAccess(const ProfileState &facts, const ContentRegistry &content,
                 const RaidReloadBinding &binding, const RaidReloadWorldAccess &access)
{
    const auto scope = actionScope(binding);
    if (scope.activity == WeaponActionActivity::Raid)
        return (!access.activity || *access.activity == scope) && binding.spaceId == access.spaceId &&
            validRaidSpace(facts,content,access.spaceId,access.playerCenter);
    return access.activity && *access.activity == scope && boundedPoint(access.playerCenter,access.worldSize) &&
        (scope.activity != WeaponActionActivity::BaseDefense ||
         (facts.activeBaseDefense && access.worldSize.x == facts.activeBaseDefense->worldSize.x &&
          access.worldSize.y == facts.activeBaseDefense->worldSize.y));
}
bool droppedForScope(const AssetRecord &asset, const WeaponActionScope &scope)
{
    if (scope.activity == WeaponActionActivity::Raid)
    { const auto *d=std::get_if<RaidDroppedMagazineLocation>(&asset.location); return d && d->raidId==scope.activityId; }
    if (baseActivity(scope))
    { const auto *d=std::get_if<BaseGroundAssetLocation>(&asset.location); return d && d->baseSiteDefinitionId==scope.baseSiteDefinitionId; }
    const auto *d=std::get_if<TrainingDroppedMagazineLocation>(&asset.location); return d && d->sessionId==scope.activityId;
}
bool validWorldDrop(const ProfileState &facts,const ContentRegistry &content,const AssetLocation &location,
                    const RaidReloadBinding &binding,const RaidReloadWorldAccess &access)
{
    const auto scope=actionScope(binding);
    Vec2 point;
    if(scope.activity==WeaponActionActivity::Raid)
    {
        const auto *d=std::get_if<RaidDroppedMagazineLocation>(&location);
        if(!d || d->raidId!=scope.activityId || d->spaceId!=access.spaceId || !validRaidSpace(facts,content,d->spaceId,d->position))return false;
        point=d->position;
    }
    else if(baseActivity(scope))
    {
        const auto *d=std::get_if<BaseGroundAssetLocation>(&location);
        if(!d || d->baseSiteDefinitionId!=scope.baseSiteDefinitionId || !boundedPoint(d->position,access.worldSize))return false;
        point=d->position;
    }
    else
    {
        const auto *d=std::get_if<TrainingDroppedMagazineLocation>(&location);
        if(!d || d->sessionId!=scope.activityId || !boundedPoint(d->position,access.worldSize))return false;
        point=d->position;
    }
    const float dx=point.x-access.playerCenter.x,dy=point.y-access.playerCenter.y;
    return validAccess(facts,content,binding,access) && dx*dx+dy*dy<=32.0F*32.0F;
}
void updateBaseRecovery(ProfileState &candidate,const RaidReloadStageCommand &command)
{
    if(!baseActivity(actionScope(command.binding)))return;
    if(command.stage==RaidReloadStage::Chamber){candidate.baseReloadRecovery.reset();return;}
    candidate.baseReloadRecovery=BaseReloadRecoveryState{actionScope(command.binding),command.binding.weaponAssetId,
        command.binding.newMagazineAssetId,command.binding.oldMagazineAssetId,command.binding.actionId,
        command.newSource.location,command.newSource.orientation,
        command.oldReturn ? std::optional<StoredAssetLocation>{command.oldReturn->location}:std::nullopt,
        command.oldReturn ? command.oldReturn->orientation:ItemOrientation::Degrees0,command.kind,command.source};
}

RaidReloadReceipt reloadFailure(ProfileRevision revision, DomainErrorCode error, std::string message)
{
    RaidReloadReceipt result;
    result.error = error; result.message = std::move(message); result.revision = revision;
    return result;
}

RaidReloadReceipt applyReloadStage(ProfileState &candidate, const ProfileState &facts,
    const ContentRegistry &content, const RaidReloadStageCommand &command, const RaidReloadWorldAccess &access)
{
    const auto fail = [&](std::string message) { return reloadFailure(candidate.revision,
        DomainErrorCode::IllegalDestination, std::move(message)); };
    const auto &binding = command.binding;
    if ((command.kind != RaidReloadKind::Normal && command.kind != RaidReloadKind::Emergency) ||
        (command.source != RaidReloadSource::AutomaticChestRig && command.source != RaidReloadSource::SelectedChestRig &&
         command.source != RaidReloadSource::ExplicitStored) ||
        !validBinding(candidate, facts, content, binding) || !validAccess(facts, content, binding, access))
        return fail("Raid reload binding or current space changed");
    auto *weapon = candidate.assets.findMutable(binding.weaponAssetId);
    auto *fresh = candidate.assets.findMutable(binding.newMagazineAssetId);
    auto *old = binding.oldMagazineAssetId ? candidate.assets.findMutable(*binding.oldMagazineAssetId) : nullptr;
    if (!fresh || !validMagazine(*fresh, content, weapon->definitionId) ||
        (binding.oldMagazineAssetId && (!old || !validMagazine(*old, content, weapon->definitionId))))
        return fail("Raid reload magazine participants changed");
    const auto current = installedMagazine(candidate, binding.weaponAssetId);
    RaidReloadReceipt receipt; receipt.succeeded = true; receipt.revision = candidate.revision;
    receipt.committedStage = command.stage;
    const auto oldDetached = [&]() {
        if (!old) return true;
        if (command.kind == RaidReloadKind::Normal)
            return heldBy(old, binding, RaidReloadMagazineRole::OldMagazine);
        return droppedForScope(*old,actionScope(binding));
    };
    switch (command.stage)
    {
    case RaidReloadStage::TakeNew:
    {
        const auto *stored = std::get_if<StoredAssetLocation>(&fresh->location);
        if (!stored || *stored != command.newSource.location || fresh->orientation != command.newSource.orientation ||
            current != binding.oldMagazineAssetId ||
            (!assetIsCarried(candidate,fresh->instanceId) && !(baseActivity(actionScope(binding)) &&
                command.source==RaidReloadSource::ExplicitStored && access.stashAccessible && stored->container==ProfileContainerId::stash())) ||
            (command.source != RaidReloadSource::ExplicitStored && !isChestRigStored(candidate, *stored)) ||
            (command.source == RaidReloadSource::AutomaticChestRig && fresh->magazineRounds.empty()))
            return fail("Raid reload source no longer matches the selected stored magazine");
        for (const auto &[id, asset] : candidate.assets.records())
            if (anyHeldByWeapon(asset,binding.weaponAssetId))
                return fail("weapon already has a held reload magazine");
        fresh->location = heldLocation(binding,RaidReloadMagazineRole::NewMagazine);
        if (command.kind == RaidReloadKind::Normal && old)
        {
            auto destinations = magazineContainers(candidate, content, command.source != RaidReloadSource::ExplicitStored);
            if(baseActivity(actionScope(binding)) && command.source==RaidReloadSource::ExplicitStored && access.stashAccessible)
                destinations.push_back(ProfileContainerId::stash());
            if (!command.oldReturn || std::find(destinations.begin(), destinations.end(),
                command.oldReturn->location.container) == destinations.end() || !profilePlacementFits(candidate, content,
                command.oldReturn->location.container, command.oldReturn->location.origin,
                content.item(old->definitionId), command.oldReturn->orientation, old->instanceId))
                return fail("reserved old magazine destination is no longer available");
        }
        updateBaseRecovery(candidate,command); return receipt;
    }
    case RaidReloadStage::RemoveOld:
        if (!heldBy(fresh, binding, RaidReloadMagazineRole::NewMagazine) || current != binding.oldMagazineAssetId)
            return fail("old magazine removal is out of sequence");
        if (!old) { updateBaseRecovery(candidate,command); return receipt; }
        if (command.kind == RaidReloadKind::Normal)
            old->location = heldLocation(binding,RaidReloadMagazineRole::OldMagazine);
        else
        {
            const auto drop=command.worldDrop ? command.worldDrop : command.drop ? std::optional<AssetLocation>{*command.drop}:std::nullopt;
            if (!drop || !validWorldDrop(facts,content,*drop,binding,access))
                return fail("emergency reload has no legal current-space drop");
            old->location = *drop;
            receipt.droppedMagazineAssetIds.push_back(old->instanceId);
        }
        receipt.magazineRemoved = true;
        updateBaseRecovery(candidate,command); return receipt;
    case RaidReloadStage::InsertNew:
        if (current || !heldBy(fresh, binding, RaidReloadMagazineRole::NewMagazine) || !oldDetached())
            return fail("new magazine insertion is out of sequence");
        fresh->location = InstalledMagazineLocation{binding.weaponAssetId};
        receipt.magazineInserted = true;
        updateBaseRecovery(candidate,command); return receipt;
    case RaidReloadStage::StowOld:
        if (current != std::optional<AssetInstanceId>{fresh->instanceId} || command.kind != RaidReloadKind::Normal ||
            (old && !heldBy(old, binding, RaidReloadMagazineRole::OldMagazine)))
            return fail("old magazine stow is out of sequence");
        if (!old) { updateBaseRecovery(candidate,command); return receipt; }
        if (!command.oldReturn ||
            (command.source != RaidReloadSource::ExplicitStored && !isChestRigStored(candidate, command.oldReturn->location)) ||
            (!assetIsCarried(candidate,command.oldReturn->location.container.ownerAssetId) &&
                !(baseActivity(actionScope(binding)) && command.source==RaidReloadSource::ExplicitStored &&
                  access.stashAccessible && command.oldReturn->location.container==ProfileContainerId::stash())) ||
            !profilePlacementFits(candidate, content,
            command.oldReturn->location.container, command.oldReturn->location.origin,
            content.item(old->definitionId), command.oldReturn->orientation, old->instanceId))
            return fail("reserved old magazine destination is occupied");
        old->location = command.oldReturn->location; old->orientation = command.oldReturn->orientation;
        updateBaseRecovery(candidate,command); return receipt;
    case RaidReloadStage::Chamber:
        if (current != std::optional<AssetInstanceId>{fresh->instanceId} ||
            (old && command.kind == RaidReloadKind::Normal && !std::holds_alternative<StoredAssetLocation>(old->location)) ||
            (old && command.kind == RaidReloadKind::Emergency && !oldDetached()))
            return fail("reload chamber step is out of sequence");
        receipt.chamberedRound = feedChamber(*weapon, fresh);
        updateBaseRecovery(candidate,command); return receipt;
    }
    return fail("unknown Raid reload stage");
}

RaidReloadReceipt applyReloadCancel(ProfileState &candidate, const ProfileState &facts,
    const ContentRegistry &content, const RaidReloadCancelCommand &command, const RaidReloadWorldAccess &access)
{
    if (!validBinding(candidate, facts, content, command.binding) ||
        !validAccess(facts, content, command.binding, access))
        return reloadFailure(candidate.revision, DomainErrorCode::IllegalDestination, "reload cancel binding changed");
    for (const auto &[id,asset]:candidate.assets.records())
        if(anyHeldByWeapon(asset,command.binding.weaponAssetId) &&
           !heldBy(&asset,command.binding,RaidReloadMagazineRole::NewMagazine) &&
           !heldBy(&asset,command.binding,RaidReloadMagazineRole::OldMagazine))
            return reloadFailure(candidate.revision,DomainErrorCode::IllegalDestination,"reload cancel participants changed");
    RaidReloadReceipt receipt;receipt.succeeded=true;receipt.revision=candidate.revision;
    const auto resolve=[&](AssetInstanceId id,RaidReloadMagazineRole role,std::optional<RaidMagazinePlacement> preferred,
                           const std::optional<AssetLocation> &drop) {
        auto *asset=candidate.assets.findMutable(id);
        if(!heldBy(asset,command.binding,role))return true;
        const bool allowStash=baseActivity(actionScope(command.binding)) && access.stashAccessible &&
            command.newSource.location.container==ProfileContainerId::stash();
        if(const auto placement=magazineReturnPlacement(candidate,content,*asset,true,false,preferred,allowStash))
        {asset->location=placement->location;asset->orientation=placement->orientation;return true;}
        if(!drop || !validWorldDrop(facts,content,*drop,command.binding,access))return false;
        asset->location=*drop;receipt.droppedMagazineAssetIds.push_back(id);return true;
    };
    const auto newDrop=command.newWorldFallbackDrop ? command.newWorldFallbackDrop :
        command.newFallbackDrop ? std::optional<AssetLocation>{*command.newFallbackDrop}:std::nullopt;
    const auto oldDrop=command.oldWorldFallbackDrop ? command.oldWorldFallbackDrop :
        command.oldFallbackDrop ? std::optional<AssetLocation>{*command.oldFallbackDrop}:std::nullopt;
    if(!resolve(command.binding.newMagazineAssetId,RaidReloadMagazineRole::NewMagazine,command.newSource,newDrop) ||
       (command.binding.oldMagazineAssetId && !resolve(*command.binding.oldMagazineAssetId,RaidReloadMagazineRole::OldMagazine,std::nullopt,oldDrop)))
        return reloadFailure(candidate.revision,DomainErrorCode::Capacity,"held magazines cannot be safely resolved");
    if(baseActivity(actionScope(command.binding)))candidate.baseReloadRecovery.reset();
    return receipt;
}

template<class Apply>
RaidReloadReceipt executeReloadTransaction(ProfileState &profile, const ContentRegistry &content,
                                           const CommandContext &context, Apply apply)
{
    if (context.transactionId.empty()) return reloadFailure(profile.revision, DomainErrorCode::InvalidTransaction, "empty transaction ID");
    if (profile.committedTransactions.contains(context.transactionId))
    {
        RaidReloadReceipt receipt; receipt.succeeded = true; receipt.alreadyCommitted = true;
        receipt.revision = profile.revision; return receipt;
    }
    if (context.expectedRevision != profile.revision) return reloadFailure(profile.revision, DomainErrorCode::StaleRevision, "profile revision is stale");
    if (profile.revision == std::numeric_limits<ProfileRevision>::max())
        return reloadFailure(profile.revision, DomainErrorCode::RevisionOverflow, "profile revision cannot advance");
    auto candidate = magazineScratch(profile);
    auto receipt = apply(candidate);
    if (!receipt.succeeded) return receipt;
    // The only potentially throwing operation precedes the no-throw ownership commit.
    profile.committedTransactions.insert(context.transactionId);
    profile.assets = std::move(candidate.assets);
    profile.baseReloadRecovery = std::move(candidate.baseReloadRecovery);
    ++profile.revision; receipt.revision = profile.revision;
    (void)content;
    return receipt;
}
}

RaidReloadPlan queryRaidReload(const ProfileState &profile, const ContentRegistry &content,
                              const RaidReloadRequest &request)
{
    RaidReloadPlan plan;
    plan.revision = profile.revision; plan.binding = request.binding; plan.kind = request.kind; plan.source = request.source;
    const auto fail = [&](std::string message) { plan.error = DomainErrorCode::IllegalDestination; plan.message = std::move(message); return plan; };
    if ((request.kind != RaidReloadKind::Normal && request.kind != RaidReloadKind::Emergency) ||
        (request.source != RaidReloadSource::AutomaticChestRig && request.source != RaidReloadSource::SelectedChestRig &&
         request.source != RaidReloadSource::ExplicitStored)) return fail("unknown reload route or source");
    if (!validActivity(profile,actionScope(plan.binding))) return fail("no matching active weapon context");
    const auto *weapon = profile.assets.find(plan.binding.weaponAssetId);
    if (!weapon) return fail("weapon does not exist");
    if (!plan.binding.newMagazineAssetId && request.source == RaidReloadSource::AutomaticChestRig)
    {
        for (const auto &[id, asset] : profile.assets.records())
        {
            const auto *stored = std::get_if<StoredAssetLocation>(&asset.location);
            if (!stored || !isChestRigStored(profile, *stored) || asset.magazineRounds.empty() ||
                !content.magazineFitsWeapon(asset.definitionId, weapon->definitionId)) continue;
            const auto *best = profile.assets.find(plan.binding.newMagazineAssetId);
            if (!best || asset.magazineRounds.size() > best->magazineRounds.size() ||
                (asset.magazineRounds.size() == best->magazineRounds.size() && id < best->instanceId))
                plan.binding.newMagazineAssetId = id;
        }
    }
    const auto current = installedMagazine(profile, plan.binding.weaponAssetId);
    if (plan.binding.oldMagazineAssetId && current != plan.binding.oldMagazineAssetId) return fail("old magazine changed");
    plan.binding.oldMagazineAssetId = current;
    if (!validBinding(profile, profile, content, plan.binding)) return fail("invalid Raid reload binding");
    const auto *fresh = profile.assets.find(plan.binding.newMagazineAssetId);
    const auto *stored = fresh ? std::get_if<StoredAssetLocation>(&fresh->location) : nullptr;
    const bool allowStash=baseActivity(actionScope(plan.binding)) && request.source==RaidReloadSource::ExplicitStored && request.stashAccessible;
    if (!fresh || !stored || (!assetIsCarried(profile,fresh->instanceId) &&
        !(allowStash && stored->container==ProfileContainerId::stash())) || !validMagazine(*fresh, content, weapon->definitionId) ||
        (request.source != RaidReloadSource::ExplicitStored && !isChestRigStored(profile, *stored)) ||
        (request.source == RaidReloadSource::AutomaticChestRig && fresh->magazineRounds.empty()))
        return fail("selected reload magazine is unavailable");
    for (const auto &[id, asset] : profile.assets.records())
        if (anyHeldByWeapon(asset,plan.binding.weaponAssetId))
            return fail("weapon already has held magazines");
    plan.newSource = {*stored, fresh->orientation};
    if (request.kind == RaidReloadKind::Normal && current)
    {
        auto scratch = magazineScratch(profile);
        scratch.assets.findMutable(fresh->instanceId)->location = heldLocation(plan.binding,RaidReloadMagazineRole::NewMagazine);
        const auto *old = scratch.assets.find(*current);
        plan.oldReturn = magazineReturnPlacement(scratch, content, *old, true,
            request.source != RaidReloadSource::ExplicitStored,
            RaidMagazinePlacement{*stored, old->orientation},allowStash);
        if (!plan.oldReturn) { plan.error = DomainErrorCode::Capacity; plan.message = "no legal place to retain the old magazine"; return plan; }
    }
    plan.canCommit = true; return plan;
}

std::vector<RaidReloadMagazineChoice> queryRaidReloadMagazines(const ProfileState &profile,
    const ContentRegistry &content, AssetInstanceId weaponId)
{
    std::vector<RaidReloadMagazineChoice> result;
    const auto *weapon = profile.assets.find(weaponId);
    if (!weapon) return result;
    for (const auto &[id, asset] : profile.assets.records())
    {
        const auto *stored = std::get_if<StoredAssetLocation>(&asset.location);
        if (!stored || !isChestRigStored(profile, *stored) ||
            !content.magazineFitsWeapon(asset.definitionId, weapon->definitionId)) continue;
        if (!profile.pendingRaid)
        {
            const auto plan = queryWeaponAmmo(profile, content, InstallMagazineAndChamberCommand{weaponId, id});
            result.push_back({id, static_cast<std::uint32_t>(asset.magazineRounds.size()), plan.canCommit, plan.error});
            continue;
        }
        RaidReloadRequest request;
        request.binding = {profile.pendingRaid->raidId, outdoorRaidSpaceId(), weaponId,
            "reload-choice", id, installedMagazine(profile, weaponId)};
        request.source = RaidReloadSource::SelectedChestRig;
        const auto plan = queryRaidReload(profile, content, request);
        result.push_back({id, static_cast<std::uint32_t>(asset.magazineRounds.size()), plan.canCommit, plan.error});
    }
    std::stable_sort(result.begin(), result.end(), [](const auto &a, const auto &b) {
        return a.roundCount != b.roundCount ? a.roundCount > b.roundCount : a.magazineAssetId < b.magazineAssetId;
    });
    return result;
}

InventoryPlan queryRaidReloadStage(const ProfileState &profile, const ContentRegistry &content,
    const RaidReloadStageCommand &command, const RaidReloadWorldAccess &access)
{
    auto candidate = magazineScratch(profile);
    const auto result = applyReloadStage(candidate, profile, content, command, access);
    return {result.succeeded, result.error, result.message, profile.revision};
}

RaidReloadReceipt executeRaidReloadStage(ProfileState &profile, const ContentRegistry &content,
    const RaidReloadStageCommand &command, const CommandContext &context, const RaidReloadWorldAccess &access)
{
    return executeReloadTransaction(profile, content, context, [&](auto &candidate) {
        return applyReloadStage(candidate, profile, content, command, access);
    });
}

InventoryPlan queryRaidReloadCancel(const ProfileState &profile, const ContentRegistry &content,
    const RaidReloadCancelCommand &command, const RaidReloadWorldAccess &access)
{
    auto candidate = magazineScratch(profile);
    const auto result = applyReloadCancel(candidate, profile, content, command, access);
    return {result.succeeded, result.error, result.message, profile.revision};
}

RaidReloadReceipt executeRaidReloadCancel(ProfileState &profile, const ContentRegistry &content,
    const RaidReloadCancelCommand &command, const CommandContext &context, const RaidReloadWorldAccess &access)
{
    if (context.transactionId.empty()) return reloadFailure(profile.revision, DomainErrorCode::InvalidTransaction, "empty transaction ID");
    if (!profile.committedTransactions.contains(context.transactionId) && context.expectedRevision != profile.revision)
        return reloadFailure(profile.revision, DomainErrorCode::StaleRevision, "profile revision is stale");
    if (validBinding(profile, profile, content, command.binding) && validAccess(profile, content, command.binding, access))
    {
        const bool anyHeld = std::any_of(profile.assets.records().begin(), profile.assets.records().end(), [&](const auto &entry) {
            return anyHeldByWeapon(entry.second,command.binding.weaponAssetId);
        });
        if (!anyHeld && !profile.baseReloadRecovery)
        {
            RaidReloadReceipt receipt; receipt.succeeded = true; receipt.revision = profile.revision;
            receipt.alreadyCommitted = profile.committedTransactions.contains(context.transactionId); return receipt;
        }
    }
    return executeReloadTransaction(profile, content, context, [&](auto &candidate) {
        return applyReloadCancel(candidate, profile, content, command, access);
    });
}

InventoryReceipt pickupDroppedMagazine(ProfileState &profile,const ContentRegistry &content,
    AssetInstanceId id,const CommandContext &context,const RaidReloadWorldAccess &access)
{
    const auto result=executeReloadTransaction(profile,content,context,[&](auto &candidate){
        const auto *asset=candidate.assets.find(id);
        Vec2 point{};
        bool owned{};
        if(asset)
        {
            if(const auto *drop=std::get_if<RaidDroppedMagazineLocation>(&asset->location))
            {owned=profile.pendingRaid && drop->raidId==profile.pendingRaid->raidId && drop->spaceId==access.spaceId;point=drop->position;}
            else if(const auto *drop=std::get_if<TrainingDroppedMagazineLocation>(&asset->location))
            {owned=access.activity && access.activity->activity==WeaponActionActivity::Training && weaponActionScopeMatches(profile,*access.activity) && drop->sessionId==access.activity->activityId;point=drop->position;}
            else if(const auto *drop=std::get_if<BaseGroundAssetLocation>(&asset->location))
            {owned=access.activity && baseActivity(*access.activity) && weaponActionScopeMatches(profile,*access.activity) && drop->baseSiteDefinitionId==access.activity->baseSiteDefinitionId;point=drop->position;}
        }
        const auto *view=access.observation;const auto *key=access.currentObservation;
        if(!owned || !view || !key || key->spaceId!=access.spaceId || !view->matches(*key) ||
           !view->matches(access.spaceId,access.playerCenter) || !view->boundsVisible({{point.x-12,point.y-12},{24,24}}))
            return reloadFailure(profile.revision,DomainErrorCode::IllegalDestination,"magazine is outside current observation");
        const float dx=point.x-access.playerCenter.x,dy=point.y-access.playerCenter.y;
        if(!std::isfinite(dx) || !std::isfinite(dy) || dx*dx+dy*dy>72.0F*72.0F)
            return reloadFailure(profile.revision,DomainErrorCode::IllegalDestination,"magazine is too far away");
        if(content.item(asset->definitionId).category!=ItemCategory::Magazine || asset->quantity!=1)
            return reloadFailure(profile.revision,DomainErrorCode::InvalidProfile,"dropped asset is not one magazine");
        const auto destination=magazineReturnPlacement(candidate,content,*asset,true,false);
        if(!destination)return reloadFailure(profile.revision,DomainErrorCode::Capacity,"space required for magazine");
        auto *moving=candidate.assets.findMutable(id);moving->location=destination->location;moving->orientation=destination->orientation;
        RaidReloadReceipt receipt;receipt.succeeded=true;receipt.revision=profile.revision;return receipt;
    });
    return {result.succeeded,result.alreadyCommitted,result.error,result.message,result.revision};
}

ProfileValidationResult retainRaidReloadMagazinesForLossInCandidate(
    ProfileState &candidate, const ContentRegistry &content, std::string_view raidId)
{
    for (const auto &[id, asset] : candidate.assets.records())
    {
        const auto *held = std::get_if<RaidReloadHeldMagazineLocation>(&asset.location);
        if (!held) continue;
        const auto *weapon = candidate.assets.find(held->weaponAssetId);
        if (held->raidId != raidId || !knownReloadRole(held->role) || !weapon ||
            !std::holds_alternative<EquippedAssetLocation>(weapon->location) ||
            !validMagazine(asset, content, weapon->definitionId))
            return {false, "held magazine cannot be retained with its failed-Raid weapon"};
        candidate.assets.findMutable(id)->location = RetainedRaidMagazineLocation{held->weaponAssetId, held->role};
    }
    return {true, {}};
}

ProfileValidationResult resolveRetainedRaidMagazinesInCandidate(ProfileState &candidate,
    const ContentRegistry &content, AssetInstanceId weaponId, bool raidCarriedDestination)
{
    std::vector<AssetInstanceId> ids;
    for (const auto &[id, asset] : candidate.assets.records())
        if (const auto *retained = std::get_if<RetainedRaidMagazineLocation>(&asset.location);
            retained && retained->weaponAssetId == weaponId) ids.push_back(id);
    std::stable_sort(ids.begin(), ids.end(), [&](auto a, auto b) {
        return std::get<RetainedRaidMagazineLocation>(candidate.assets.find(a)->location).role <
               std::get<RetainedRaidMagazineLocation>(candidate.assets.find(b)->location).role;
    });
    for (auto id : ids)
    {
        auto *asset = candidate.assets.findMutable(id);
        const auto destination = magazineReturnPlacement(candidate, content, *asset, raidCarriedDestination, false);
        if (!destination) return {false, "not enough space to recover detached magazines with the weapon"};
        asset->location = destination->location; asset->orientation = destination->orientation;
    }
    return {true, {}};
}

ProfileValidationResult validateRaidMagazineLocations(const ProfileState &profile, const ContentRegistry &content)
{
    try
    {
    std::set<std::pair<AssetInstanceId, RaidReloadMagazineRole>> heldRoles, retainedRoles;
    std::map<AssetInstanceId, std::pair<std::string, RaidSpaceDefinitionId>> heldBindings;
    for (const auto &[id, asset] : profile.assets.records())
    {
        if (const auto *drop = std::get_if<RaidDroppedMagazineLocation>(&asset.location))
        {
            if (!profile.pendingRaid || drop->raidId != profile.pendingRaid->raidId ||
                content.item(asset.definitionId).category != ItemCategory::Magazine || asset.quantity != 1 ||
                !validRaidSpace(profile, content, drop->spaceId, drop->position))
                return {false, "invalid Raid dropped magazine ownership"};
        }
        if (const auto *held = std::get_if<RaidReloadHeldMagazineLocation>(&asset.location))
        {
            const auto *weapon = profile.assets.find(held->weaponAssetId);
            if (!profile.pendingRaid || held->raidId != profile.pendingRaid->raidId || held->actionId.empty() ||
                !knownReloadRole(held->role) || !validRaidSpace(profile, content, held->spaceId) || !weapon ||
                !std::holds_alternative<EquippedAssetLocation>(weapon->location) ||
                !validMagazine(asset, content, weapon->definitionId) ||
                !heldRoles.emplace(held->weaponAssetId, held->role).second)
                return {false, "invalid Raid held magazine ownership"};
            const auto [binding, inserted] = heldBindings.emplace(held->weaponAssetId,
                std::pair{held->actionId, held->spaceId});
            if (!inserted && binding->second != std::pair{held->actionId, held->spaceId})
                return {false, "held magazines disagree on their reload action"};
        }
        if (const auto *retained = std::get_if<RetainedRaidMagazineLocation>(&asset.location))
        {
            const auto *weapon = profile.assets.find(retained->weaponAssetId);
            bool allowed = weapon && (std::holds_alternative<LostRaidAssetLocation>(weapon->location) ||
                std::holds_alternative<RecoveryTaskAssetLocation>(weapon->location));
            if (weapon && profile.pendingRaid && profile.pendingRaid->selfRecovery && profile.pendingRaid->selfRecovery->opened)
            {
                const auto *ground = std::get_if<RaidGroundAssetLocation>(&weapon->location);
                allowed = allowed || (ground && ground->raidId == profile.pendingRaid->raidId &&
                    std::any_of(profile.pendingRaid->selfRecovery->roots.begin(), profile.pendingRaid->selfRecovery->roots.end(),
                        [&](const auto &root) { return root.assetId == weapon->instanceId && root.lootSlotIndex == ground->lootSlotIndex; }));
            }
            if (!allowed || !knownReloadRole(retained->role) || !validMagazine(asset, content, weapon->definitionId) ||
                !retainedRoles.emplace(retained->weaponAssetId, retained->role).second)
                return {false, "invalid retained Raid magazine ownership"};
            if (profile.recoveryTask && std::holds_alternative<RecoveryTaskAssetLocation>(weapon->location) &&
                profile.recoveryTask->recoveredAssetIds.contains(id) != profile.recoveryTask->recoveredAssetIds.contains(weapon->instanceId))
                return {false, "retained magazine recovery differs from weapon"};
        }
    }
    return {true, {}};
    }
    catch (...) { return {false, "Raid magazine ownership references unknown content"}; }
}

ProfileValidationResult validateBaseMagazineRecovery(const ProfileState &profile,const ContentRegistry &content)
{
    try
    {
        const auto *recovery=profile.baseReloadRecovery ? &*profile.baseReloadRecovery:nullptr;
        std::optional<RaidReloadBinding> binding;
        if(recovery)
        {
            binding=RaidReloadBinding{"",{},recovery->weaponAssetId,recovery->actionId,
                recovery->newMagazineAssetId,recovery->oldMagazineAssetId,recovery->activity};
            if(!baseActivity(recovery->activity) || !validBinding(profile,profile,content,*binding) ||
               (recovery->kind!=RaidReloadKind::Normal && recovery->kind!=RaidReloadKind::Emergency) ||
               (recovery->source!=RaidReloadSource::AutomaticChestRig && recovery->source!=RaidReloadSource::SelectedChestRig && recovery->source!=RaidReloadSource::ExplicitStored) ||
               (recovery->newSource.container.kind!=ProfileContainerKind::AssetCompartment &&
                !(recovery->source==RaidReloadSource::ExplicitStored && recovery->newSource.container==ProfileContainerId::stash())) ||
               (recovery->newSource.container.kind==ProfileContainerKind::AssetCompartment && !profile.assets.find(recovery->newSource.container.ownerAssetId)) ||
               !canUseItemOrientation(content.item(profile.assets.find(recovery->newMagazineAssetId)->definitionId),recovery->newSourceOrientation))
                return {false,"invalid Base reload recovery binding"};
            const auto *fresh=profile.assets.find(recovery->newMagazineAssetId);
            if(!profilePlacementWithinContainer(profile,content,recovery->newSource.container,recovery->newSource.origin,
                content.item(fresh->definitionId),recovery->newSourceOrientation))
                return {false,"Base reload original source bounds are invalid"};
            if(recovery->oldReturn)
            {
                const auto *old=recovery->oldMagazineAssetId ? profile.assets.find(*recovery->oldMagazineAssetId):nullptr;
                if(!old || !profilePlacementWithinContainer(profile,content,recovery->oldReturn->container,recovery->oldReturn->origin,
                    content.item(old->definitionId),recovery->oldReturnOrientation))
                    return {false,"Base reload reserved return bounds are invalid"};
            }
            const auto installed=installedMagazine(profile,recovery->weaponAssetId);
            if(!heldBy(fresh,*binding,RaidReloadMagazineRole::NewMagazine) && installed!=recovery->newMagazineAssetId)
                return {false,"Base reload new magazine has no physical action state"};
            if(recovery->oldMagazineAssetId)
            {
                const auto *old=profile.assets.find(*recovery->oldMagazineAssetId);
                if(installed!=recovery->oldMagazineAssetId && !heldBy(old,*binding,RaidReloadMagazineRole::OldMagazine) &&
                   !std::holds_alternative<StoredAssetLocation>(old->location) && !droppedForScope(*old,recovery->activity))
                    return {false,"Base reload old magazine has no physical action state"};
            }
        }
        std::set<std::pair<std::string,RaidReloadMagazineRole>> roles;
        for(const auto &[id,asset]:profile.assets.records())
        {
            if(const auto *held=std::get_if<BaseReloadHeldMagazineLocation>(&asset.location))
            {
                if(!binding || !knownReloadRole(held->role) || !heldBy(&asset,*binding,held->role) ||
                   (held->role==RaidReloadMagazineRole::NewMagazine ? id!=binding->newMagazineAssetId : binding->oldMagazineAssetId!=id) ||
                   !roles.emplace(held->actionId,held->role).second)
                    return {false,"Base held magazine has no unique recovery owner"};
            }
            if(const auto *held=std::get_if<TrainingHeldMagazineLocation>(&asset.location))
            {
                const auto *weapon=profile.assets.find(held->weaponAssetId);
                if(profile.pendingRaid || profile.activeBaseDefense || held->sessionId.empty() || held->actionId.empty() ||
                   !knownReloadRole(held->role) || !weapon || !std::holds_alternative<EquippedAssetLocation>(weapon->location) ||
                   !validMagazine(asset,content,weapon->definitionId) || !roles.emplace(held->sessionId+"/"+held->actionId,held->role).second)
                    return {false,"invalid isolated training held magazine"};
            }
            if(const auto *drop=std::get_if<TrainingDroppedMagazineLocation>(&asset.location))
                if(profile.pendingRaid || profile.activeBaseDefense || drop->sessionId.empty() ||
                   !std::isfinite(drop->position.x) || !std::isfinite(drop->position.y) || drop->position.x<0 || drop->position.y<0 ||
                   content.item(asset.definitionId).category!=ItemCategory::Magazine || asset.quantity!=1)
                    return {false,"invalid isolated training dropped magazine"};
        }
        return {true,{}};
    }
    catch(...){return {false,"Base magazine recovery references unknown content"};}
}

RaidReloadReceipt recoverBaseReloadInCandidate(ProfileState &candidate,const ContentRegistry &content,
                                              const RaidReloadWorldAccess &access)
{
    if(!candidate.baseReloadRecovery){RaidReloadReceipt receipt;receipt.succeeded=true;receipt.revision=candidate.revision;return receipt;}
    const auto recovery=*candidate.baseReloadRecovery;
    RaidReloadBinding binding{"",{},recovery.weaponAssetId,recovery.actionId,
        recovery.newMagazineAssetId,recovery.oldMagazineAssetId,recovery.activity};
    RaidReloadCancelCommand command{binding,{recovery.newSource,recovery.newSourceOrientation},{},{}};
    command.newWorldFallbackDrop=BaseGroundAssetLocation{recovery.activity.baseSiteDefinitionId,access.playerCenter};
    command.oldWorldFallbackDrop=command.newWorldFallbackDrop;
    return applyReloadCancel(candidate,candidate,content,command,access);
}

bool weaponActionScopeMatches(const ProfileState &profile,const WeaponActionScope &scope) noexcept
{
    try{return validActivity(profile,scope);}catch(...){return false;}
}
