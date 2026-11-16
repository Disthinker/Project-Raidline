#include "self_recovery_domain.h"

#include <algorithm>
#include <cmath>
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
    if (recovery.pursuit)
    {
        return failure(DomainErrorCode::IllegalDestination,
            "THE PURSUIT TARGET HAS NO STATIC CACHE", candidate.revision);
    }
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

namespace
{
bool recoveryObserved(const ProfileState &profile, const RaidVisibilityProjection *view,
                      const RaidObservationKey *current)
{
    if(!profile.pendingRaid || !profile.pendingRaid->selfRecovery || !view || !current ||
       profile.pendingRaid->selfRecovery->pursuit ||
       current->spaceId != outdoorRaidSpaceId() || !view->matches(*current)) return false;
    const auto p=profile.pendingRaid->selfRecovery->cachePosition;
    const float dx=p.x-current->observer.x,dy=p.y-current->observer.y;
    return dx*dx+dy*dy <= 82*82 && view->boundsVisible({{p.x-30,p.y-22},{60,44}});
}
}

RaidSelfRecoveryPlan queryOpenRaidSelfRecovery(
    const ProfileState &profile,
    const ContentRegistry &content,
    const RaidVisibilityProjection *observation,
    const RaidObservationKey *currentObservation)
{
    if(!recoveryObserved(profile,observation,currentObservation))
        return {false,DomainErrorCode::IllegalDestination,"Lost cache is outside current observation",profile.revision,0};
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
    const CommandContext &context,
    const RaidVisibilityProjection *observation,
    const RaidObservationKey *currentObservation)
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

    if(!recoveryObserved(profile,observation,currentObservation))
        return failure(DomainErrorCode::IllegalDestination,"Lost cache is outside current observation",profile.revision);
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

bool raidLostWeaponCorpsePointClear(
    const MapDefinition &map, const PendingRaidSnapshot &raid, Vec2 point) noexcept
{
    constexpr float halfWidth = 26.0F;
    constexpr float halfHeight = 18.0F;
    const auto &bounds = map.walkableBounds;
    if (!std::isfinite(point.x) || !std::isfinite(point.y) ||
        point.x - halfWidth < bounds.position.x ||
        point.y - halfHeight < bounds.position.y ||
        point.x + halfWidth > bounds.position.x + bounds.size.x ||
        point.y + halfHeight > bounds.position.y + bounds.size.y)
        return false;

    const auto overlaps = [&](const ContentRect &blocker)
    {
        return point.x + halfWidth > blocker.position.x &&
            point.y + halfHeight > blocker.position.y &&
            point.x - halfWidth < blocker.position.x + blocker.size.x &&
            point.y - halfHeight < blocker.position.y + blocker.size.y;
    };
    return std::none_of(raid.spatialLayout.ballisticBlockers.begin(),
        raid.spatialLayout.ballisticBlockers.end(), overlaps);
}

bool revealRaidLostWeaponPursuitCorpse(
    ProfileState &profile, const ContentRegistry &content, Vec2 position) noexcept
{
    try
    {
        if (!profile.pendingRaid || !profile.pendingRaid->selfRecovery ||
            !profile.pendingRaid->selfRecovery->pursuit)
            return false;
        auto &raid = *profile.pendingRaid;
        auto &pursuit = *raid.selfRecovery->pursuit;
        auto *root = profile.assets.findMutable(pursuit.corpseAssetId);
        auto *scene = root ? std::get_if<RaidSceneContainerAssetLocation>(&root->location) : nullptr;
        if (!scene || root->definitionId != kRaidLostWeaponPursuitCorpseId ||
            raid.mapDefinitionId != kFieldIntelligenceSourceMapId ||
            scene->raidId != raid.raidId || scene->spaceId != outdoorRaidSpaceId() ||
            !raidLostWeaponCorpsePointClear(content.map(raid.mapDefinitionId), raid, position))
            return false;
        if (pursuit.deathPosition)
            return true;
        if (pursuit.claimed || profile.revision == std::numeric_limits<ProfileRevision>::max())
            return false;

        scene->position = position;
        pursuit.deathPosition = position;
        ++profile.revision;
        return true;
    }
    catch (...)
    {
        return false;
    }
}
