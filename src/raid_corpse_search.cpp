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

bool revealRaidCorpseInSimulation(ProfileState &profile, const ContentRegistry &content, const RaidCorpseDeathFact &fact) noexcept
{
    try
    {
        if (!profile.pendingRaid || !trial(*profile.pendingRaid) || !finite(fact.position) ||
            fact.spaceId != outdoorRaidSpaceId() || fact.sourceEnemyId.empty() ||
            !validateRaidCorpseBindings(profile, content).valid) return false;
        const auto &raid = *profile.pendingRaid;
        for (const auto &[id, root] : profile.assets.records())
        {
            const auto *corpse = std::get_if<RaidCorpseContainerAssetLocation>(&root.location);
            if (!corpse || corpse->sourceEnemyId != fact.sourceEnemyId || corpse->spaceId != fact.spaceId) continue;
            if (!id || root.instanceId != id || id >= profile.assets.nextAssetId() || corpse->raidId != raid.raidId) return false;
            // Preserve the strict entry's once-only rule, including collected loot
            // and a finite repeated fact at a different position or max revision.
            if (corpse->deathPosition) return true;
            if (profile.revision == std::numeric_limits<ProfileRevision>::max()) return false;
            const auto size = content.map(raid.mapDefinitionId).worldSize;
            if (!finite(size) || size.x <= 0 || size.y <= 0 || fact.position.x < 0 || fact.position.y < 0 ||
                fact.position.x > size.x || fact.position.y > size.y) return false;
            const auto loot = std::find_if(raid.loot.begin(), raid.loot.end(),
                [id](const auto &entry) { return entry.corpseContainerAssetId == id; });
            if (loot == raid.loot.end() || loot->collected || !loot->assetId || loot->assetId == id ||
                loot->assetId >= profile.assets.nextAssetId() || !finite(loot->position) ||
                loot->position.x < 0 || loot->position.y < 0 || loot->position.x > size.x || loot->position.y > size.y) return false;
            if (std::count_if(raid.loot.begin(), raid.loot.end(),
                    [&](const auto &entry) { return entry.assetId == loot->assetId; }) != 1) return false;
            const auto point = std::find_if(raid.spatialLayout.resourcePoints.begin(), raid.spatialLayout.resourcePoints.end(),
                [&](const auto &value) { return value.instanceId == loot->resourcePointInstanceId; });
            if (point == raid.spatialLayout.resourcePoints.end() || loot->resourcePointSlotIndex >= point->capacity) return false;
            std::size_t pointBase{};
            for (auto it = raid.spatialLayout.resourcePoints.begin(); it != point; ++it) pointBase += it->capacity;
            const auto expected = raidResourcePointLootPosition(*point, loot->resourcePointSlotIndex);
            if (loot->slotIndex != pointBase + loot->resourcePointSlotIndex ||
                loot->position.x != expected.x || loot->position.y != expected.y) return false;
            const auto &table = content.lootTable(point->lootTableId);
            const auto entry = std::find_if(table.entries.begin(), table.entries.end(),
                [&](const auto &value) { return value.itemDefinitionId == loot->definitionId; });
            if (entry == table.entries.end() || loot->quantity < entry->minimumQuantity || loot->quantity > entry->maximumQuantity) return false;
            const auto *item = profile.assets.find(loot->assetId);
            const auto *stored = item ? std::get_if<StoredAssetLocation>(&item->location) : nullptr;
            if (!item || item->instanceId != loot->assetId || item->definitionId != loot->definitionId ||
                !loot->quantity || item->quantity != loot->quantity || !stored ||
                stored->container != ProfileContainerId::compartment(id, 0)) return false;
            const auto &definition = content.item(item->definitionId);
            const auto &rootDefinition = content.item(root.definitionId);
            if (item->quantity > definition.maxStackSize || !canUseItemOrientation(definition, item->orientation) ||
                rootDefinition.containerCompartments.empty()) return false;
            const auto footprint = inventoryFootprint(definition, item->orientation);
            const auto &compartment = rootDefinition.containerCompartments.front();
            if (stored->origin.x < 0 || stored->origin.y < 0 || footprint.width <= 0 || footprint.height <= 0 ||
                footprint.width > compartment.width || footprint.height > compartment.height ||
                stored->origin.x > compartment.width - footprint.width ||
                stored->origin.y > compartment.height - footprint.height) return false;
            // An unrevealed root owns only its one frozen roll. Nothing can be
            // deposited through a legal inventory operation before the reveal.
            for (const auto &[otherId, other] : profile.assets.records())
                if (const auto *location = std::get_if<StoredAssetLocation>(&other.location);
                    location && location->container.kind == ProfileContainerKind::AssetCompartment &&
                    location->container.ownerAssetId == id && otherId != loot->assetId) return false;
            auto *mutableRoot = profile.assets.findMutable(id);
            auto *location = mutableRoot ? std::get_if<RaidCorpseContainerAssetLocation>(&mutableRoot->location) : nullptr;
            if (!location) return false;
            // All fallible queries/allocation precede these nonthrowing writes.
            location->deathPosition = fact.position;
            ++profile.revision;
            return true;
        }
    }
    catch (...)
    {
        return false;
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
