#include "noise_decoy_domain.h"
#include <limits>

bool noiseDecoyInChestRig(const ProfileState &profile, const ContentRegistry &content, AssetInstanceId id)
{
    const auto *asset = profile.assets.find(id);
    const auto chest = equippedAsset(profile, EquipmentSlotKind::ChestRig);
    const auto *rig = chest ? profile.assets.find(*chest) : nullptr;
    if (!asset || !rig || !asset->quantity) return false;
    const auto *stored = std::get_if<StoredAssetLocation>(&asset->location);
    if (!stored || stored->container.kind != ProfileContainerKind::AssetCompartment ||
        stored->container.ownerAssetId != *chest) return false;
    const auto &pockets = content.item(rig->definitionId).containerCompartments;
    return stored->container.compartmentIndex < pockets.size() &&
        pockets[stored->container.compartmentIndex].pocketKind == ContainerPocketKind::General &&
        content.item(asset->definitionId).noiseDecoyUse.has_value();
}

std::vector<AssetInstanceId> chestRigNoiseDecoys(const ProfileState &profile, const ContentRegistry &content)
{
    std::vector<AssetInstanceId> result;
    for (const auto &[id, asset] : profile.assets.records())
        if (content.item(asset.definitionId).noiseDecoyUse && noiseDecoyInChestRig(profile, content, id)) result.push_back(id);
    return result; // Registry's stable ID order also defines wheel order.
}

InventoryReceipt consumeNoiseDecoy(ProfileState &profile, const ContentRegistry &content,
    AssetInstanceId id, const CommandContext &context)
{
    const auto reject = [&](DomainErrorCode error, const char *message) {
        return InventoryReceipt{false, false, error, message, profile.revision}; };
    if (context.transactionId.empty()) return reject(DomainErrorCode::InvalidTransaction, "INVALID DECOY COMMAND");
    if (profile.committedTransactions.contains(context.transactionId))
        return {true, true, DomainErrorCode::None, {}, profile.revision};
    if (context.expectedRevision != profile.revision) return reject(DomainErrorCode::StaleRevision, "DECOY INVENTORY CHANGED");
    if (profile.revision == std::numeric_limits<ProfileRevision>::max())
        return reject(DomainErrorCode::RevisionOverflow, "DECOY REVISION LIMIT");
    if (!noiseDecoyInChestRig(profile, content, id)) return reject(DomainErrorCode::MissingAsset, "NO CHEST RIG DECOY");
    if (!validateInventoryAssetGraph(profile.assets, content).valid)
        return reject(DomainErrorCode::InvalidProfile, "INVALID DECOY INVENTORY");
    // Allocate every fallible participant before the sole, nonthrowing commit.
    auto assets = profile.assets;
    auto transactions = profile.committedTransactions;
    transactions.insert(context.transactionId);
    auto *asset = assets.findMutable(id);
    if (asset->quantity == 1) static_cast<void>(assets.erase(id));
    else --asset->quantity;
    if (!validateInventoryAssetGraph(assets, content).valid)
        return reject(DomainErrorCode::InvalidProfile, "INVALID DECOY INVENTORY");
    profile.assets = std::move(assets);
    profile.committedTransactions = std::move(transactions);
    ++profile.revision;
    return {true, false, DomainErrorCode::None, {}, profile.revision};
}
