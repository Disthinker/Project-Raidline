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
        !magazineDefinition.compatibleAmmunitionDefinitionId.has_value() ||
        *magazineDefinition.compatibleAmmunitionDefinitionId !=
            ammunition->definitionId)
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
    std::map<std::optional<std::string>, std::uint32_t> quantities;
    for (const MagazineRoundRecord &round : magazine->magazineRounds)
    {
        ++quantities[round.reliefBatchId];
    }
    const ItemDefinitionId ammunitionId =
        magazine->magazineRounds.front().definitionId;
    for (const auto &[reliefBatchId, quantity] : quantities)
    {
        if (!mergeOrCreateAmmunition(
                candidate,
                content,
                command.destination,
                ammunitionId,
                reliefBatchId,
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
    if (!weaponDefinition.compatibleMagazineDefinitionId.has_value() ||
        *weaponDefinition.compatibleMagazineDefinitionId != magazine->definitionId)
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
    const auto origin = findFirstProfileFit(
        candidate,
        content,
        command.destination,
        definition,
        magazine->orientation,
        magazine->instanceId);
    if (!origin.has_value())
    {
        return failure(DomainErrorCode::Capacity,
                       "destination cannot hold magazine", candidate.revision);
    }
    magazine->location = StoredAssetLocation{command.destination, *origin};
    return {true, false, DomainErrorCode::None, {}, candidate.revision,
            WeaponAmmoResult::Uninstalled, std::nullopt};
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
        !definition.compatibleMagazineDefinitionId.has_value())
    {
        return failure(DomainErrorCode::IllegalDestination,
                       "asset is not a magazine-fed weapon", candidate.revision);
    }
    const auto installed = installedMagazine(candidate, command.weaponAssetId);
    AssetRecord *magazine = installed.has_value()
        ? candidate.assets.findMutable(*installed)
        : nullptr;
    const auto feed = [&weapon, &magazine]()
    {
        if (magazine == nullptr || magazine->magazineRounds.empty())
        {
            return false;
        }
        weapon->chamberedRound = magazine->magazineRounds.front();
        magazine->magazineRounds.erase(magazine->magazineRounds.begin());
        return true;
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
    static_cast<void>(feed());
    return {true, false, DomainErrorCode::None, {}, candidate.revision,
            WeaponAmmoResult::Fired, fired};
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
            else if constexpr (std::is_same_v<Command, UninstallMagazineCommand>)
                return applyUninstall(candidate, content, typed);
            else
                return applyFire(candidate, content, typed);
        },
        command);
}
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
    if (receipt.result == WeaponAmmoResult::Dry)
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

std::size_t magazineRoundCount(
    const ProfileState &profile,
    AssetInstanceId magazineAssetId) noexcept
{
    const AssetRecord *magazine = profile.assets.find(magazineAssetId);
    return magazine == nullptr ? 0U : magazine->magazineRounds.size();
}

