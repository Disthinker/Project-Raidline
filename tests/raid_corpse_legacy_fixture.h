#pragma once
#include "raid_corpse_search.h"

// Build historical test data, never a production downgrade/migration. Keep
// the original frozen rolls and restore their pre-corpse ground ownership.
inline void stripCorpseBindingsForLegacySnapshot(ProfileState &profile)
{
    if (!profile.pendingRaid) return;
    auto &raid = *profile.pendingRaid;
    for (auto &loot : raid.loot)
        if (loot.corpseContainerAssetId)
        {
            if (auto *asset = profile.assets.findMutable(loot.assetId))
                asset->location = RaidGroundAssetLocation{raid.raidId, loot.slotIndex};
            loot.corpseContainerAssetId = 0;
        }
    std::vector<AssetInstanceId> roots;
    for (const auto &[id, asset] : profile.assets.records())
        if (std::holds_alternative<RaidCorpseContainerAssetLocation>(asset.location)) roots.push_back(id);
    for (auto id : roots) static_cast<void>(profile.assets.erase(id));
    for (auto &enemy : raid.enemies) enemy.sourceEnemyId.clear();
    if (raid.rulesVersion == kRaidCorpseRulesVersion) raid.rulesVersion = "content-beta-loot-economy-29";
}
