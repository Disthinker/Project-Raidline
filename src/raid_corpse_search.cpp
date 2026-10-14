#include "raid_corpse_search.h"
#include "inventory_domain.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <set>

namespace
{
bool finite(Vec2 p) { return std::isfinite(p.x) && std::isfinite(p.y); }
bool trial(const PendingRaidSnapshot &raid)
{
    return raid.rulesVersion == kRaidCorpseRulesVersion &&
        raid.mapDefinitionId == MapDefinitionId{"map.raid.frontier_exchange"};
}
std::string sourceId(const RaidEncounterGroupSnapshot &group) { return group.instanceId + ".armored"; }
bool ordinary(const PendingRaidSnapshot &raid, const RaidLootSnapshot &loot)
{
    if (loot.requiresHighRisk || loot.sceneContainerAssetId || loot.corpseContainerAssetId ||
        loot.spaceId != outdoorRaidSpaceId()) return false;
    const auto point = std::find_if(raid.spatialLayout.resourcePoints.begin(), raid.spatialLayout.resourcePoints.end(),
        [&](const auto &p) { return p.instanceId == loot.resourcePointInstanceId; });
    return point != raid.spatialLayout.resourcePoints.end() && point->kind == RaidResourcePointKind::Ordinary;
}
}

ProfileValidationResult freezeRaidCorpseLoot(ProfileState &profile, const ContentRegistry &content)
{
    if (!profile.pendingRaid || !trial(*profile.pendingRaid)) return {true, {}};
    ProfileState candidate = profile;
    auto &raid = *candidate.pendingRaid;
    if (!raid.startingAssets) return {false, "corpse trial requires exact pre-deploy assets"};
    if (candidate.assets.nextAssetId() > std::numeric_limits<AssetInstanceId>::max() - 3U)
        return {false, "corpse asset identity space exhausted"};
    std::vector<const RaidEncounterGroupSnapshot *> groups;
    for (const auto &group : raid.encounterGroups)
        if (group.definitionId == "encounter.frontier.resource_guard" && group.spaceId == outdoorRaidSpaceId()) groups.push_back(&group);
    if (groups.size() != 3U) return {false, "corpse trial requires three frozen guard groups"};
    std::sort(groups.begin(), groups.end(), [](auto a, auto b) { return a->instanceId < b->instanceId; });
    for (const auto *group : groups)
    {
        if (group->memberEnemyIndices.empty() || group->memberEnemyIndices.front() >= raid.enemies.size())
            return {false, "corpse guard source is missing"};
        auto &enemy = raid.enemies[group->memberEnemyIndices.front()];
        if (!enemy.torsoArmor || !enemy.sourceEnemyId.empty()) return {false, "corpse guard source is invalid"};
        RaidLootSnapshot *selected{};
        float closest = std::numeric_limits<float>::infinity();
        for (auto &loot : raid.loot)
        {
            if (!ordinary(raid, loot)) continue;
            const float dx = loot.position.x - group->homePosition.x, dy = loot.position.y - group->homePosition.y;
            const float distance = dx * dx + dy * dy;
            if (!selected || distance < closest || (distance == closest && loot.assetId < selected->assetId))
            { selected = &loot; closest = distance; }
        }
        if (!selected) return {false, "corpse trial has insufficient ordinary frozen rolls"};
        enemy.sourceEnemyId = sourceId(*group);
        const auto root = candidate.assets.create(content.item(ItemDefinitionId{"item.container.base_storage_crate"}),
            RaidCorpseContainerAssetLocation{raid.raidId, enemy.spaceId, enemy.sourceEnemyId, std::nullopt});
        auto *asset = candidate.assets.findMutable(selected->assetId);
        if (!asset || !std::holds_alternative<RaidGroundAssetLocation>(asset->location)) return {false, "corpse Loot has no ground origin"};
        const auto container = ProfileContainerId::compartment(root, 0);
        const auto fit = findFirstProfileFit(candidate, content, container, content.item(asset->definitionId), asset->orientation, asset->instanceId);
        if (!fit) return {false, "corpse frozen Loot cannot fit"};
        asset->location = StoredAssetLocation{container, *fit};
        selected->corpseContainerAssetId = root;
    }
    profile = std::move(candidate);
    return {true, {}};
}

ProfileValidationResult validateRaidCorpseBindings(const ProfileState &profile, const ContentRegistry &content)
{
    std::set<std::string> expected, roots;
    if (profile.pendingRaid && trial(*profile.pendingRaid))
    {
        const auto &raid = *profile.pendingRaid;
        if (!raid.startingAssets) return {false, "corpse rollback baseline missing"};
        for (const auto &group : raid.encounterGroups)
            if (group.definitionId == "encounter.frontier.resource_guard")
            {
                if (group.memberEnemyIndices.empty() || group.memberEnemyIndices.front() >= raid.enemies.size()) return {false, "corpse source group invalid"};
                const auto &enemy = raid.enemies[group.memberEnemyIndices.front()];
                if (!enemy.torsoArmor || enemy.sourceEnemyId != sourceId(group) || enemy.spaceId != outdoorRaidSpaceId()) return {false, "corpse source identity invalid"};
                expected.insert(enemy.sourceEnemyId);
            }
        if (expected.size() != 3U) return {false, "corpse source count invalid"};
    }
    std::set<std::string> observed;
    if (profile.pendingRaid)
        for (const auto &enemy : profile.pendingRaid->enemies)
            if (!enemy.sourceEnemyId.empty() && (!expected.contains(enemy.sourceEnemyId) || !observed.insert(enemy.sourceEnemyId).second))
                return {false, "unknown or duplicate corpse enemy source"};
    if (observed != expected) return {false, "corpse enemy binding missing"};
    for (const auto &[id, asset] : profile.assets.records())
        if (const auto *corpse = std::get_if<RaidCorpseContainerAssetLocation>(&asset.location))
        {
            if (!profile.pendingRaid || !expected.contains(corpse->sourceEnemyId) || !roots.insert(corpse->sourceEnemyId).second ||
                corpse->raidId != profile.pendingRaid->raidId || corpse->spaceId != outdoorRaidSpaceId() ||
                asset.definitionId != ItemDefinitionId{"item.container.base_storage_crate"} || asset.quantity != 1U)
                return {false, "corpse ownership/source invalid"};
            if (corpse->deathPosition)
            {
                const auto size = content.map(profile.pendingRaid->mapDefinitionId).worldSize;
                const auto p = *corpse->deathPosition;
                if (!finite(p) || p.x < 0 || p.y < 0 || p.x > size.x || p.y > size.y) return {false, "corpse death position invalid"};
            }
            if (std::count_if(profile.pendingRaid->loot.begin(), profile.pendingRaid->loot.end(),
                [id](const auto &loot) { return loot.corpseContainerAssetId == id; }) != 1) return {false, "corpse frozen origin count invalid"};
        }
    if (roots != expected) return {false, "corpse roots missing"};
    if (profile.pendingRaid)
        for (const auto &loot : profile.pendingRaid->loot)
            if (loot.corpseContainerAssetId)
            {
                const auto *root = profile.assets.find(loot.corpseContainerAssetId);
                const auto *corpse = root ? std::get_if<RaidCorpseContainerAssetLocation>(&root->location) : nullptr;
                const auto point = std::find_if(profile.pendingRaid->spatialLayout.resourcePoints.begin(), profile.pendingRaid->spatialLayout.resourcePoints.end(),
                    [&](const auto &p) { return p.instanceId == loot.resourcePointInstanceId; });
                if (!corpse || loot.sceneContainerAssetId || loot.requiresHighRisk || loot.spaceId != corpse->spaceId ||
                    point == profile.pendingRaid->spatialLayout.resourcePoints.end() || point->kind != RaidResourcePointKind::Ordinary ||
                    (!loot.collected && raidSceneContainerRoot(profile, loot.assetId) != loot.corpseContainerAssetId))
                    return {false, "corpse Loot origin invalid"};
            }
    return {true, {}};
}

bool revealRaidCorpse(ProfileState &profile, const ContentRegistry &content, const RaidCorpseDeathFact &fact)
{
    if (!profile.pendingRaid || !finite(fact.position) || fact.spaceId != outdoorRaidSpaceId()) return false;
    for (const auto &[id, asset] : profile.assets.records())
        if (const auto *corpse = std::get_if<RaidCorpseContainerAssetLocation>(&asset.location);
            corpse && corpse->sourceEnemyId == fact.sourceEnemyId && corpse->spaceId == fact.spaceId)
        {
            if (corpse->deathPosition) return true; // Once exposed, never move or award again.
            if (profile.revision == std::numeric_limits<ProfileRevision>::max()) return false;
            ProfileState candidate = profile;
            auto &location = std::get<RaidCorpseContainerAssetLocation>(candidate.assets.findMutable(id)->location);
            location.deathPosition = fact.position;
            ++candidate.revision;
            if (!validateProfileState(candidate, content).valid) return false;
            profile = std::move(candidate);
            return true;
        }
    return false;
}

bool raidCorpseAccessible(const ProfileState &profile, AssetInstanceId id, const RaidSpaceDefinitionId &space, Vec2 player)
{
    const auto *asset = profile.assets.find(id);
    const auto *corpse = asset ? std::get_if<RaidCorpseContainerAssetLocation>(&asset->location) : nullptr;
    if (!corpse || !profile.pendingRaid || !corpse->deathPosition || !finite(player) ||
        corpse->raidId != profile.pendingRaid->raidId || corpse->spaceId != space) return false;
    const float dx = player.x - corpse->deathPosition->x, dy = player.y - corpse->deathPosition->y;
    return dx * dx + dy * dy <= 72.0F * 72.0F;
}
