#include "grenade_domain.h"

#include "thrown_illumination_domain.h"

#include <limits>
#include <utility>

namespace
{
using ValidThrowable = bool (*)(const ItemDefinition &);

bool isGrenade(const ItemDefinition &definition)
{
    return definition.grenadeUse && definition.grenadeUse->valid();
}

bool isThrownIllumination(const ItemDefinition &definition)
{
    return definition.thrownIlluminationUse && definition.thrownIlluminationUse->valid();
}

bool throwableInChestRig(const ProfileState &profile, const ContentRegistry &content,
    AssetInstanceId id, ValidThrowable valid)
{
    const auto *asset = profile.assets.find(id);
    const auto chest = equippedAsset(profile, EquipmentSlotKind::ChestRig);
    const auto *rig = chest ? profile.assets.find(*chest) : nullptr;
    if (!asset || !rig || asset->quantity == 0) return false;
    const auto *stored = std::get_if<StoredAssetLocation>(&asset->location);
    if (!stored || stored->container.kind != ProfileContainerKind::AssetCompartment ||
        stored->container.ownerAssetId != *chest) return false;
    try
    {
        const auto &pockets = content.item(rig->definitionId).containerCompartments;
        const auto &definition = content.item(asset->definitionId);
        return stored->container.compartmentIndex < pockets.size() &&
            pockets[stored->container.compartmentIndex].pocketKind == ContainerPocketKind::General &&
            valid(definition) && asset->quantity <= definition.maxStackSize;
    }
    catch (...)
    {
        return false;
    }
}

struct ThrowableRejectionMessages
{
    const char *invalidCommand;
    const char *staleRevision;
    const char *revisionLimit;
    const char *missingAsset;
    const char *invalidInventory;
};

InventoryReceipt consumeChestRigThrowable(ProfileState &profile, const ContentRegistry &content,
    AssetInstanceId id, const CommandContext &context, ValidThrowable valid,
    const ThrowableRejectionMessages &messages)
{
    const auto reject = [&](DomainErrorCode error, const char *message)
    {
        return InventoryReceipt{false, false, error, message, profile.revision};
    };
    if (context.transactionId.empty())
        return reject(DomainErrorCode::InvalidTransaction, messages.invalidCommand);
    if (profile.committedTransactions.contains(context.transactionId))
        return {true, true, DomainErrorCode::None, {}, profile.revision};
    if (context.expectedRevision != profile.revision)
        return reject(DomainErrorCode::StaleRevision, messages.staleRevision);
    if (profile.revision == std::numeric_limits<ProfileRevision>::max())
        return reject(DomainErrorCode::RevisionOverflow, messages.revisionLimit);
    if (!throwableInChestRig(profile, content, id, valid))
        return reject(DomainErrorCode::MissingAsset, messages.missingAsset);
    if (!validateInventoryAssetGraph(profile.assets, content,
            profile.stashLayoutVersion, profile.chestRigLayoutVersion).valid)
        return reject(DomainErrorCode::InvalidProfile, messages.invalidInventory);

    auto assets = profile.assets;
    auto transactions = profile.committedTransactions;
    transactions.insert(context.transactionId);
    auto *asset = assets.findMutable(id);
    if (asset->quantity == 1U) static_cast<void>(assets.erase(id));
    else --asset->quantity;
    if (!validateInventoryAssetGraph(assets, content,
            profile.stashLayoutVersion, profile.chestRigLayoutVersion).valid)
        return reject(DomainErrorCode::InvalidProfile, messages.invalidInventory);
    profile.assets = std::move(assets);
    profile.committedTransactions = std::move(transactions);
    ++profile.revision;
    return {true, false, DomainErrorCode::None, {}, profile.revision};
}
}

bool grenadeInChestRig(const ProfileState &profile, const ContentRegistry &content, AssetInstanceId id)
{
    return throwableInChestRig(profile, content, id, isGrenade);
}

bool thrownIlluminationInChestRig(
    const ProfileState &profile, const ContentRegistry &content, AssetInstanceId id)
{
    return throwableInChestRig(profile, content, id, isThrownIllumination);
}

std::vector<AssetInstanceId> chestRigGrenades(const ProfileState &profile, const ContentRegistry &content)
{
    std::vector<AssetInstanceId> result;
    for (const auto &[id, asset] : profile.assets.records())
    {
        static_cast<void>(asset);
        if (grenadeInChestRig(profile, content, id)) result.push_back(id);
    }
    return result;
}

InventoryReceipt consumeGrenade(ProfileState &profile, const ContentRegistry &content,
    AssetInstanceId id, const CommandContext &context)
{
    return consumeChestRigThrowable(profile, content, id, context, isGrenade,
        {"INVALID GRENADE COMMAND", "GRENADE INVENTORY CHANGED", "GRENADE REVISION LIMIT",
         "NO CHEST RIG GRENADE", "INVALID GRENADE INVENTORY"});
}

InventoryReceipt consumeThrownIllumination(ProfileState &profile, const ContentRegistry &content,
    AssetInstanceId id, const CommandContext &context)
{
    return consumeChestRigThrowable(profile, content, id, context, isThrownIllumination,
        {"INVALID ILLUMINATION COMMAND", "ILLUMINATION INVENTORY CHANGED", "ILLUMINATION REVISION LIMIT",
         "NO CHEST RIG ILLUMINATION", "INVALID ILLUMINATION INVENTORY"});
}
