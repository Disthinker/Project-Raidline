#include "self_recovery_domain.h"

#include <limits>

namespace
{
InventoryReceipt failure(
    DomainErrorCode error,
    std::string message,
    ProfileRevision revision)
{
    return {false, false, error, std::move(message), revision};
}

InventoryReceipt applyOpen(
    ProfileState &candidate,
    const ContentRegistry &content)
{
    if (!candidate.pendingRaid.has_value() ||
        !candidate.pendingRaid->selfRecovery.has_value())
    {
        return failure(
            DomainErrorCode::InvalidProfile,
            "Raid has no self-recovery cache",
            candidate.revision);
    }
    RaidSelfRecoverySnapshot &recovery =
        *candidate.pendingRaid->selfRecovery;
    if (recovery.opened)
    {
        return failure(
            DomainErrorCode::IllegalDestination,
            "self-recovery cache is already open",
            candidate.revision);
    }
    const auto record = candidate.lostRaidRecords.find(
        recovery.sourceRecord.recordId);
    if (record == candidate.lostRaidRecords.end() ||
        record->second != recovery.sourceRecord || recovery.roots.empty())
    {
        return failure(
            DomainErrorCode::InvalidProfile,
            "self-recovery source record is unavailable",
            candidate.revision);
    }

    for (const RaidSelfRecoveryRootSnapshot &root : recovery.roots)
    {
        AssetRecord *asset = candidate.assets.findMutable(root.assetId);
        const auto *lost = asset != nullptr
            ? std::get_if<LostRaidAssetLocation>(&asset->location)
            : nullptr;
        if (lost == nullptr || lost->recordId != recovery.sourceRecord.recordId ||
            lost->sourceSlot != root.sourceSlot)
        {
            return failure(
                DomainErrorCode::InvalidProfile,
                "self-recovery asset ownership changed",
                candidate.revision);
        }
        try
        {
            if (!itemCanEquipInSlot(
                    content.item(asset->definitionId), root.sourceSlot))
            {
                return failure(
                    DomainErrorCode::InvalidProfile,
                    "self-recovery source slot is invalid",
                    candidate.revision);
            }
        }
        catch (...)
        {
            return failure(
                DomainErrorCode::InvalidProfile,
                "self-recovery asset definition is unavailable",
                candidate.revision);
        }
    }

    for (const RaidSelfRecoveryRootSnapshot &root : recovery.roots)
    {
        AssetRecord *asset = candidate.assets.findMutable(root.assetId);
        asset->location = RaidGroundAssetLocation{
            candidate.pendingRaid->raidId,
            root.lootSlotIndex};
        candidate.pendingRaid->loot.push_back(RaidLootSnapshot{
            root.assetId,
            asset->definitionId,
            asset->quantity,
            root.lootSlotIndex,
            root.position,
            false,
            false,
            outdoorRaidSpaceId()});
    }
    candidate.lostRaidRecords.erase(record);
    if (candidate.lastRaidResult.has_value() &&
        candidate.lastRaidResult->lostRaidRecordId ==
            std::optional<std::string>{recovery.sourceRecord.recordId})
    {
        candidate.lastRaidResult->lostRaidRecordId.reset();
    }
    recovery.opened = true;

    return {true, false, DomainErrorCode::None, {}, candidate.revision};
}
}

RaidSelfRecoveryPlan queryOpenRaidSelfRecovery(
    const ProfileState &profile,
    const ContentRegistry &content)
{
    ProfileState candidate = profile;
    const InventoryReceipt receipt = applyOpen(candidate, content);
    const std::size_t rootCount = profile.pendingRaid.has_value() &&
            profile.pendingRaid->selfRecovery.has_value()
        ? profile.pendingRaid->selfRecovery->roots.size()
        : 0U;
    return {receipt.succeeded, receipt.error, receipt.message,
            profile.revision, rootCount};
}

InventoryReceipt executeOpenRaidSelfRecovery(
    ProfileState &profile,
    const ContentRegistry &content,
    const CommandContext &context)
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
        return {true, true, DomainErrorCode::None, {}, profile.revision};
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

    ProfileState candidate = profile;
    InventoryReceipt receipt = applyOpen(candidate, content);
    if (!receipt.succeeded)
    {
        receipt.revision = profile.revision;
        return receipt;
    }
    candidate.committedTransactions.insert(context.transactionId);
    ++candidate.revision;
    const ProfileValidationResult validation = validateProfileState(
        candidate, content);
    if (!validation.valid)
    {
        return failure(
            DomainErrorCode::InvalidProfile,
            validation.message,
            profile.revision);
    }
    profile = std::move(candidate);
    receipt.revision = profile.revision;
    return receipt;
}
