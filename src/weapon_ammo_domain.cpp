#include "weapon_ammo_domain.h"

#include <algorithm>
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

WeaponAmmoReceipt applyFire(
    ProfileState &candidate,
    const ContentRegistry &content,
    const FireWeaponCommand &command)
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
    const AssetRecord *weapon = profile.assets.find(command.weaponAssetId);
    if (weapon == nullptr)
    {
        return failure(DomainErrorCode::MissingAsset,
                       "weapon does not exist", profile.revision);
    }
    const ItemDefinition &definition = content.item(weapon->definitionId);
    if (definition.category != ItemCategory::Weapon ||
        definition.compatibleMagazineDefinitionIds.empty())
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
                WeaponAmmoResult::Chambered, std::nullopt};
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
            weaponDefinition.compatibleMagazineDefinitionIds.empty())
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

WeaponAmmoReceipt apply(
    ProfileState &candidate,
    const ContentRegistry &content,
    const WeaponAmmoCommand &command)
{
    return std::visit(
        [&candidate, &content](const auto &typed)
        {
            using Command = std::decay_t<decltype(typed)>;
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
            else
                return applyClearMalfunction(candidate, content, typed);
        },
        command);
}
}

WeaponAmmoPlan queryWeaponAmmo(
    const ProfileState &profile,
    const ContentRegistry &content,
    const WeaponAmmoCommand &command)
{
    ProfileState candidate;
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

    ProfileState candidate = profile;
    WeaponAmmoReceipt receipt = apply(candidate, content, command);
    if (!receipt.succeeded)
    {
        return receipt;
    }
    if (receipt.result == WeaponAmmoResult::Dry ||
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
