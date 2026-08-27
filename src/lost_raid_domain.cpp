#include "lost_raid_domain.h"

#include <algorithm>
#include <set>

std::optional<std::string> lostRaidRecordForAsset(
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
        if (const auto *lost =
                std::get_if<LostRaidAssetLocation>(&asset->location))
        {
            return lost->recordId;
        }
        if (const auto *stored =
                std::get_if<StoredAssetLocation>(&asset->location))
        {
            if (stored->container.kind !=
                ProfileContainerKind::AssetCompartment)
            {
                return std::nullopt;
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
        return std::nullopt;
    }
    return std::nullopt;
}

std::vector<LostRaidRecordProjection> queryLostRaidRecords(
    const ProfileState &profile,
    const ContentRegistry &content)
{
    std::vector<LostRaidRecordProjection> result;
    result.reserve(profile.lostRaidRecords.size());
    for (const auto &[recordId, record] : profile.lostRaidRecords)
    {
        LostRaidRecordProjection projection;
        projection.recordId = recordId;
        projection.raidId = record.raidId;
        projection.mapDefinitionId = record.mapDefinitionId;
        projection.mapDisplayName =
            content.map(record.mapDefinitionId).displayName;
        projection.difficulty = record.difficulty;
        projection.outcome = record.outcome;
        projection.createdWorldMinute = record.createdWorldMinute;
        projection.subsequentRaidSettlementCount =
            record.subsequentRaidSettlementCount;
        projection.retainedSettlementsRemaining =
            kLostRaidRecordRetainedSettlementCount -
            std::min(
                kLostRaidRecordRetainedSettlementCount,
                record.subsequentRaidSettlementCount);

        for (const auto &[assetId, asset] : profile.assets.records())
        {
            const std::optional<std::string> owner =
                lostRaidRecordForAsset(profile, assetId);
            if (!owner.has_value() || *owner != recordId)
            {
                continue;
            }
            projection.assets.push_back(LostRaidAssetProjection{
                assetId,
                asset.definitionId,
                asset.quantity,
                std::holds_alternative<LostRaidAssetLocation>(
                    asset.location)});
        }
        result.push_back(std::move(projection));
    }
    std::sort(
        result.begin(), result.end(),
        [](const LostRaidRecordProjection &left,
           const LostRaidRecordProjection &right)
        {
            if (left.createdWorldMinute != right.createdWorldMinute)
            {
                return left.createdWorldMinute > right.createdWorldMinute;
            }
            return left.recordId > right.recordId;
        });
    return result;
}

LostRaidAgingPreview queryLostRaidAging(
    const ProfileState &profile) noexcept
{
    LostRaidAgingPreview result;
    result.activeRecordCount = static_cast<std::uint32_t>(
        profile.lostRaidRecords.size());
    for (const auto &[recordId, record] : profile.lostRaidRecords)
    {
        if (record.subsequentRaidSettlementCount <
            kLostRaidRecordRetainedSettlementCount)
        {
            continue;
        }
        ++result.recordsExpiringOnNextSettlement;
        for (const auto &[assetId, asset] : profile.assets.records())
        {
            static_cast<void>(asset);
            const std::optional<std::string> owner =
                lostRaidRecordForAsset(profile, assetId);
            if (owner.has_value() && *owner == recordId)
            {
                ++result.assetInstancesExpiringOnNextSettlement;
            }
        }
    }
    return result;
}

