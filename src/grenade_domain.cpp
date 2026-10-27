#include "grenade_domain.h"

#include <limits>
#include <utility>

bool grenadeInChestRig(const ProfileState &profile, const ContentRegistry &content, AssetInstanceId id)
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
            definition.grenadeUse.has_value() && definition.grenadeUse->valid() &&
            asset->quantity <= definition.maxStackSize;
    }
    catch (...)
    {
        return false;
    }
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
    const auto reject = [&](DomainErrorCode error, const char *message)
    {
        return InventoryReceipt{false, false, error, message, profile.revision};
    };
    if (context.transactionId.empty())
        return reject(DomainErrorCode::InvalidTransaction, "INVALID GRENADE COMMAND");
    if (profile.committedTransactions.contains(context.transactionId))
        return {true, true, DomainErrorCode::None, {}, profile.revision};
    if (context.expectedRevision != profile.revision)
        return reject(DomainErrorCode::StaleRevision, "GRENADE INVENTORY CHANGED");
    if (profile.revision == std::numeric_limits<ProfileRevision>::max())
        return reject(DomainErrorCode::RevisionOverflow, "GRENADE REVISION LIMIT");
    if (!grenadeInChestRig(profile, content, id))
        return reject(DomainErrorCode::MissingAsset, "NO CHEST RIG GRENADE");
    if (!validateInventoryAssetGraph(profile.assets, content,
            profile.stashLayoutVersion, profile.chestRigLayoutVersion).valid)
        return reject(DomainErrorCode::InvalidProfile, "INVALID GRENADE INVENTORY");

    auto assets = profile.assets;
    auto transactions = profile.committedTransactions;
    transactions.insert(context.transactionId);
    auto *asset = assets.findMutable(id);
    if (asset->quantity == 1U) static_cast<void>(assets.erase(id));
    else --asset->quantity;
    if (!validateInventoryAssetGraph(assets, content,
            profile.stashLayoutVersion, profile.chestRigLayoutVersion).valid)
        return reject(DomainErrorCode::InvalidProfile, "INVALID GRENADE INVENTORY");
    profile.assets = std::move(assets);
    profile.committedTransactions = std::move(transactions);
    ++profile.revision;
    return {true, false, DomainErrorCode::None, {}, profile.revision};
}
