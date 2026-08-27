#include "recovery_task_domain.h"

#include <algorithm>
#include <limits>
#include <set>
#include <string_view>
#include <utility>

#include "lost_raid_domain.h"

namespace
{
RecoveryTaskQuote quoteFailure(
    const ProfileState &profile,
    DomainErrorCode error,
    std::string message)
{
    return {false, error, std::move(message), profile.revision};
}

RecoveryTaskReceipt receiptFailure(
    const ProfileState &profile,
    DomainErrorCode error,
    std::string message)
{
    return {false, false, error, std::move(message), profile.revision};
}

std::optional<AssetInstanceId> parentAssetId(
    const AssetRecord &asset) noexcept
{
    if (const auto *stored =
            std::get_if<StoredAssetLocation>(&asset.location);
        stored != nullptr && stored->container.kind ==
            ProfileContainerKind::AssetCompartment)
    {
        return stored->container.ownerAssetId;
    }
    if (const auto *installed =
            std::get_if<InstalledMagazineLocation>(&asset.location))
    {
        return installed->weaponAssetId;
    }
    return std::nullopt;
}

std::uint64_t stableHashAppend(
    std::uint64_t hash,
    std::string_view value) noexcept
{
    constexpr std::uint64_t prime = 1099511628211ULL;
    for (const unsigned char byte : value)
    {
        hash ^= byte;
        hash *= prime;
    }
    return hash;
}

std::uint64_t stableHashInteger(
    std::uint64_t hash,
    std::uint64_t value) noexcept
{
    for (std::size_t index{}; index < sizeof(value); ++index)
    {
        const char byte = static_cast<char>((value >> (index * 8U)) & 0xffU);
        hash = stableHashAppend(hash, std::string_view{&byte, 1U});
    }
    return hash;
}

bool recoverAsset(
    const ProfileState &profile,
    const ContentRegistry &content,
    const LostRaidRecord &record,
    RecoveryTaskId taskId,
    AssetInstanceId assetId)
{
    const RecoveryTendency tendency = recoveryTendency(
        profile, content, record, assetId);
    const std::uint32_t baseChance =
        tendency == RecoveryTendency::High ? 80U : 35U;
    const std::uint32_t agePenalty = std::min(
        30U, record.subsequentRaidSettlementCount * 10U);
    const std::uint32_t chance = baseChance > agePenalty
        ? baseChance - agePenalty
        : 1U;

    std::uint64_t hash = 1469598103934665603ULL;
    hash = stableHashAppend(hash, profile.profileId);
    hash = stableHashAppend(hash, record.recordId);
    hash = stableHashInteger(hash, taskId);
    hash = stableHashInteger(hash, assetId);
    return hash % 100U < chance;
}

std::vector<AssetInstanceId> assetsOwnedByRecord(
    const ProfileState &profile,
    const std::string &recordId)
{
    std::vector<AssetInstanceId> result;
    for (const auto &[assetId, asset] : profile.assets.records())
    {
        static_cast<void>(asset);
        const std::optional<std::string> owner =
            lostRaidRecordForAsset(profile, assetId);
        if (owner.has_value() && *owner == recordId)
        {
            result.push_back(assetId);
        }
    }
    return result;
}

std::vector<AssetInstanceId> assetsOwnedByTask(
    const ProfileState &profile,
    RecoveryTaskId taskId)
{
    std::vector<AssetInstanceId> result;
    for (const auto &[assetId, asset] : profile.assets.records())
    {
        static_cast<void>(asset);
        const std::optional<RecoveryTaskId> owner =
            recoveryTaskForAsset(profile, assetId);
        if (owner.has_value() && *owner == taskId)
        {
            result.push_back(assetId);
        }
    }
    return result;
}

bool validContext(
    const ProfileState &profile,
    const CommandContext &context,
    RecoveryTaskReceipt &receipt)
{
    if (context.transactionId.empty())
    {
        receipt = receiptFailure(
            profile, DomainErrorCode::InvalidTransaction,
            "transaction ID must not be empty");
        return false;
    }
    if (profile.committedTransactions.contains(context.transactionId))
    {
        receipt = {true, true, DomainErrorCode::None, {}, profile.revision};
        return false;
    }
    if (context.expectedRevision != profile.revision)
    {
        receipt = receiptFailure(
            profile, DomainErrorCode::StaleRevision,
            "profile revision is stale");
        return false;
    }
    return true;
}
}

std::optional<RecoveryTaskId> recoveryTaskForAsset(
    const ProfileState &profile,
    AssetInstanceId assetId) noexcept
{
    std::set<AssetInstanceId> visited;
    AssetInstanceId current = assetId;
    while (current != 0U && visited.insert(current).second)
    {
        const AssetRecord *asset = profile.assets.find(current);
        if (asset == nullptr)
        {
            return std::nullopt;
        }
        if (const auto *task =
                std::get_if<RecoveryTaskAssetLocation>(&asset->location))
        {
            return task->taskId;
        }
        const std::optional<AssetInstanceId> parent = parentAssetId(*asset);
        if (!parent.has_value())
        {
            return std::nullopt;
        }
        current = *parent;
    }
    return std::nullopt;
}

RecoveryTendency recoveryTendency(
    const ProfileState &profile,
    const ContentRegistry &content,
    const LostRaidRecord &record,
    AssetInstanceId assetId)
{
    static_cast<void>(record);
    const AssetRecord &asset = *profile.assets.find(assetId);
    const ItemDefinition &definition = content.item(asset.definitionId);
    if (definition.category == ItemCategory::Weapon ||
        definition.category == ItemCategory::ProtectiveGear)
    {
        return RecoveryTendency::High;
    }
    if (definition.category == ItemCategory::Container)
    {
        if (const auto *lost =
                std::get_if<LostRaidAssetLocation>(&asset.location))
        {
            return lost->sourceSlot == EquipmentSlotKind::Backpack
                ? RecoveryTendency::Low
                : RecoveryTendency::High;
        }
    }
    return RecoveryTendency::Low;
}

RecoveryTaskQuote queryStartRecoveryTask(
    const ProfileState &profile,
    const ContentRegistry &content,
    const std::string &recordId)
{
    if (profile.pendingRaid.has_value())
    {
        return quoteFailure(
            profile, DomainErrorCode::IllegalDestination,
            "recovery task is unavailable during a Raid");
    }
    if (profile.recoveryTask.has_value())
    {
        return quoteFailure(
            profile, DomainErrorCode::Capacity,
            "only one recovery task may be active");
    }
    const auto record = profile.lostRaidRecords.find(recordId);
    if (record == profile.lostRaidRecords.end())
    {
        return quoteFailure(
            profile, DomainErrorCode::MissingAsset,
            "lost Raid record does not exist");
    }
    if (profile.revision == std::numeric_limits<ProfileRevision>::max() ||
        profile.nextRecoveryTaskId ==
            std::numeric_limits<RecoveryTaskId>::max())
    {
        return quoteFailure(
            profile, DomainErrorCode::RevisionOverflow,
            "recovery task identity cannot advance");
    }
    const MapDefinition &map = content.map(record->second.mapDefinitionId);
    if (profile.currency < map.recovery.serviceFee)
    {
        return quoteFailure(
            profile, DomainErrorCode::InvalidQuantity,
            "insufficient currency for recovery task");
    }
    if (profile.worldClock.elapsedWorldMinutes >
        std::numeric_limits<std::uint64_t>::max() -
            map.recovery.durationMinutes)
    {
        return quoteFailure(
            profile, DomainErrorCode::Capacity,
            "recovery completion time would overflow");
    }

    RecoveryTaskQuote result;
    result.canCommit = true;
    result.revision = profile.revision;
    result.recordId = recordId;
    result.serviceFee = map.recovery.serviceFee;
    result.durationMinutes = map.recovery.durationMinutes;
    for (AssetInstanceId assetId : assetsOwnedByRecord(profile, recordId))
    {
        if (recoveryTendency(
                profile, content, record->second, assetId) ==
            RecoveryTendency::High)
        {
            ++result.highTendencyAssets;
        }
        else
        {
            ++result.lowTendencyAssets;
        }
    }
    return result;
}

RecoveryTaskReceipt executeStartRecoveryTask(
    ProfileState &profile,
    const ContentRegistry &content,
    const std::string &recordId,
    const CommandContext &context)
{
    RecoveryTaskReceipt contextReceipt;
    if (!validContext(profile, context, contextReceipt))
    {
        return contextReceipt;
    }
    const RecoveryTaskQuote quote = queryStartRecoveryTask(
        profile, content, recordId);
    if (!quote.canCommit)
    {
        return receiptFailure(profile, quote.error, quote.message);
    }

    ProfileState candidate = profile;
    const LostRaidRecord record = candidate.lostRaidRecords.at(recordId);
    const RecoveryTaskId taskId = candidate.nextRecoveryTaskId++;
    const std::vector<AssetInstanceId> assetIds =
        assetsOwnedByRecord(candidate, recordId);
    RecoveryTask task{
        taskId,
        record,
        quote.serviceFee,
        candidate.worldClock.elapsedWorldMinutes,
        candidate.worldClock.elapsedWorldMinutes + quote.durationMinutes};
    for (AssetInstanceId assetId : assetIds)
    {
        if (recoverAsset(candidate, content, record, taskId, assetId))
        {
            task.recoveredAssetIds.insert(assetId);
        }
        AssetRecord *asset = candidate.assets.findMutable(assetId);
        if (auto *lost =
                std::get_if<LostRaidAssetLocation>(&asset->location))
        {
            asset->location = RecoveryTaskAssetLocation{
                taskId, lost->sourceSlot};
        }
    }
    candidate.lostRaidRecords.erase(recordId);
    if (candidate.lastRaidResult.has_value() &&
        candidate.lastRaidResult->lostRaidRecordId == recordId)
    {
        candidate.lastRaidResult->lostRaidRecordId.reset();
    }
    candidate.currency -= quote.serviceFee;
    candidate.recoveryTask = std::move(task);
    candidate.committedTransactions.insert(context.transactionId);
    ++candidate.revision;
    const ProfileValidationResult validation = validateProfileState(
        candidate, content);
    if (!validation.valid)
    {
        return receiptFailure(
            profile, DomainErrorCode::InvalidProfile, validation.message);
    }
    profile = std::move(candidate);
    return {true, false, DomainErrorCode::None, {}, profile.revision,
            taskId, quote.serviceFee};
}

RecoveryTaskReceipt executeCancelRecoveryTask(
    ProfileState &profile,
    const ContentRegistry &content,
    const CommandContext &context)
{
    RecoveryTaskReceipt contextReceipt;
    if (!validContext(profile, context, contextReceipt))
    {
        return contextReceipt;
    }
    if (!profile.recoveryTask.has_value())
    {
        return receiptFailure(
            profile, DomainErrorCode::MissingAsset,
            "no recovery task is active");
    }
    if (profile.revision == std::numeric_limits<ProfileRevision>::max())
    {
        return receiptFailure(
            profile, DomainErrorCode::RevisionOverflow,
            "profile revision cannot advance");
    }

    ProfileState candidate = profile;
    const RecoveryTask task = *candidate.recoveryTask;
    for (AssetInstanceId assetId : assetsOwnedByTask(candidate, task.taskId))
    {
        AssetRecord *asset = candidate.assets.findMutable(assetId);
        if (auto *location =
                std::get_if<RecoveryTaskAssetLocation>(&asset->location))
        {
            asset->location = LostRaidAssetLocation{
                task.sourceRecord.recordId, location->sourceSlot};
        }
    }
    candidate.lostRaidRecords.emplace(
        task.sourceRecord.recordId, task.sourceRecord);
    candidate.recoveryTask.reset();
    candidate.committedTransactions.insert(context.transactionId);
    ++candidate.revision;
    const ProfileValidationResult validation = validateProfileState(
        candidate, content);
    if (!validation.valid)
    {
        return receiptFailure(
            profile, DomainErrorCode::InvalidProfile, validation.message);
    }
    profile = std::move(candidate);
    return {true, false, DomainErrorCode::None, {}, profile.revision,
            task.taskId};
}

RecoveryTaskReceipt executeCollectRecoveryTask(
    ProfileState &profile,
    const ContentRegistry &content,
    const CommandContext &context)
{
    RecoveryTaskReceipt contextReceipt;
    if (!validContext(profile, context, contextReceipt))
    {
        return contextReceipt;
    }
    if (!profile.recoveryTask.has_value() ||
        !profile.recoveryTask->readyForCollection)
    {
        return receiptFailure(
            profile, DomainErrorCode::IllegalDestination,
            "recovery task is not ready for collection");
    }
    if (profile.revision == std::numeric_limits<ProfileRevision>::max())
    {
        return receiptFailure(
            profile, DomainErrorCode::RevisionOverflow,
            "profile revision cannot advance");
    }

    ProfileState candidate = profile;
    const RecoveryTask task = *candidate.recoveryTask;
    const std::vector<AssetInstanceId> owned =
        assetsOwnedByTask(candidate, task.taskId);
    for (AssetInstanceId assetId : owned)
    {
        if (!task.recoveredAssetIds.contains(assetId))
        {
            continue;
        }
        const AssetRecord *asset = candidate.assets.find(assetId);
        const std::optional<AssetInstanceId> parent = parentAssetId(*asset);
        if (parent.has_value() &&
            task.recoveredAssetIds.contains(*parent))
        {
            continue;
        }
        const ItemDefinition &definition = content.item(asset->definitionId);
        const std::optional<GridPosition> origin = findFirstProfileFit(
            candidate, content, ProfileContainerId::stash(), definition,
            asset->orientation, assetId);
        if (!origin.has_value())
        {
            return receiptFailure(
                profile, DomainErrorCode::Capacity,
                "Stash has no room for recovered assets");
        }
        candidate.assets.findMutable(assetId)->location = StoredAssetLocation{
            ProfileContainerId::stash(), *origin};
    }

    std::uint32_t recovered{};
    std::uint32_t lost{};
    for (AssetInstanceId assetId : owned)
    {
        if (task.recoveredAssetIds.contains(assetId))
        {
            ++recovered;
        }
        else
        {
            static_cast<void>(candidate.assets.erase(assetId));
            ++lost;
        }
    }
    candidate.recoveryTask.reset();
    candidate.committedTransactions.insert(context.transactionId);
    ++candidate.revision;
    const ProfileValidationResult validation = validateProfileState(
        candidate, content);
    if (!validation.valid)
    {
        return receiptFailure(
            profile, DomainErrorCode::InvalidProfile, validation.message);
    }
    profile = std::move(candidate);
    return {true, false, DomainErrorCode::None, {}, profile.revision,
            task.taskId, 0U, recovered, lost};
}

RecoveryTaskAdvanceResult applyRecoveryTaskThrough(
    ProfileState &profile) noexcept
{
    if (!profile.recoveryTask.has_value() ||
        profile.recoveryTask->readyForCollection ||
        profile.worldClock.elapsedWorldMinutes <
            profile.recoveryTask->completionWorldMinute)
    {
        return {};
    }
    profile.recoveryTask->readyForCollection = true;
    return {true};
}

std::optional<RecoveryTaskProjection> queryRecoveryTask(
    const ProfileState &profile,
    const ContentRegistry &content)
{
    if (!profile.recoveryTask.has_value())
    {
        return std::nullopt;
    }
    const RecoveryTask &task = *profile.recoveryTask;
    const std::uint64_t remaining = task.readyForCollection ||
            profile.worldClock.elapsedWorldMinutes >= task.completionWorldMinute
        ? 0U
        : task.completionWorldMinute -
            profile.worldClock.elapsedWorldMinutes;
    return RecoveryTaskProjection{
        task.taskId,
        task.sourceRecord.recordId,
        task.sourceRecord.mapDefinitionId,
        content.map(task.sourceRecord.mapDefinitionId).displayName,
        task.paidCurrency,
        task.startedWorldMinute,
        task.completionWorldMinute,
        remaining,
        task.readyForCollection,
        static_cast<std::uint32_t>(task.recoveredAssetIds.size()),
        static_cast<std::uint32_t>(
            assetsOwnedByTask(profile, task.taskId).size())};
}
