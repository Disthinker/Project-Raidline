#include "raid_corpse_search.h"
#include "raid_lifecycle.h"
#include "inventory_domain.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <set>

namespace
{
const ItemDefinitionId kPistol{"item.weapon.pistol_basic"};
const ItemDefinitionId kMagazine{"item.magazine.9mm_15_pistol"};
const ItemDefinitionId kRound{"item.ammunition.9mm_basic"};
const ItemDefinitionId kContainer{"item.container.base_storage_crate"};
constexpr std::array<std::string_view, 2> kSources{"riverside-scavenger:0", "riverside-scavenger:1"};

bool ordinaryRiverside(const PendingRaidSnapshot &raid)
{
    return raid.mapDefinitionId == MapDefinitionId{"map.raid.riverside"} && !raid.selfRecovery &&
        !raid.outpostRestoration && !raid.baseSiteClearance && !raid.basePerimeterSweep && !raid.fireSupport;
}

// Each post sits beside the bus. Use the side opposite this raid's trader.
// The northern and side perimeter paths remain outside both 400-unit circles.
std::optional<Vec2> post(const MapDefinition &map, const PendingRaidSnapshot &raid, std::size_t index, Vec2 size)
{
    const auto bus = std::find_if(map.ballisticBlockers.begin(), map.ballisticBlockers.end(),
        [](const auto &blocker) { return blocker.id == "south_bus"; });
    if (bus == map.ballisticBlockers.end() || !raid.fieldIntelligenceTrade) return std::nullopt;
    const auto &bounds = bus->bounds;
    const bool north = raid.fieldIntelligenceTrade->position.y > bounds.position.y;
    const float x = bounds.position.x + (index == 0 ? 125.0F : 190.0F);
    return Vec2{x - size.x * 0.5F, north ? bounds.position.y - size.y - 2.0F :
        bounds.position.y + bounds.size.y + 2.0F};
}

bool storedIn(const AssetRecord &asset, AssetInstanceId id)
{
    const auto *location = std::get_if<StoredAssetLocation>(&asset.location);
    return location && location->container == ProfileContainerId::compartment(id, 0);
}

bool installedIn(const AssetRecord &asset, AssetInstanceId weaponId)
{
    const auto *installed = std::get_if<InstalledMagazineLocation>(&asset.location);
    return installed && installed->weaponAssetId == weaponId;
}

bool ordinaryRound(const MagazineRoundRecord &round)
{
    return round.definitionId == kRound && !round.reliefBatchId;
}

bool validHeldEquipment(const AssetRegistry &assets, const RaidArmedScavengerSnapshot &actor)
{
    const auto *gun = assets.find(actor.pistolAssetId);
    const auto *first = assets.find(actor.firstMagazineId);
    const auto *second = assets.find(actor.secondMagazineId);
    if (!gun || !first || !second || gun->definitionId != kPistol || first->definitionId != kMagazine ||
        second->definitionId != kMagazine || !storedIn(*gun, actor.corpseAssetId) ||
        (gun->chamberedRound && !ordinaryRound(*gun->chamberedRound))) return false;
    const bool swapped = installedIn(*second, gun->instanceId);
    const auto &active = swapped ? *second : *first;
    const auto &spare = swapped ? *first : *second;
    return installedIn(active, gun->instanceId) && storedIn(spare, actor.corpseAssetId) &&
        active.magazineRounds.size() <= 5U && (gun->chamberedRound || active.magazineRounds.empty()) &&
        spare.magazineRounds.size() == (swapped ? 0U : 6U) &&
        std::all_of(active.magazineRounds.begin(), active.magazineRounds.end(), ordinaryRound) &&
        std::all_of(spare.magazineRounds.begin(), spare.magazineRounds.end(), ordinaryRound);
}
}

ProfileValidationResult freezeRaidArmedScavengersInCandidate(ProfileState &profile, const ContentRegistry &content)
{
    if (!profile.pendingRaid || !ordinaryRiverside(*profile.pendingRaid) ||
        !supportsRiversideArmedScavengersContent(content.contentVersion())) return {false, "unsupported armed encounter"};
    auto &raid = *profile.pendingRaid;
    if (!raid.armedScavengers.empty()) return validateRaidArmedScavengers(profile, content);
    if (raid.enemies.size() < 2U || !raid.startingAssets ||
        profile.assets.nextAssetId() > std::numeric_limits<AssetInstanceId>::max() - 8U)
        return {false, "armed encounter deployment is incomplete"};
    const auto &map = content.map(raid.mapDefinitionId);
    std::set<std::string> routes;
    std::vector<RaidLootSnapshot *> budget;
    for (auto &loot : raid.loot)
    {
        if (loot.requiresHighRisk || loot.spaceId != outdoorRaidSpaceId() ||
            loot.slotIndex >= map.raidLootSlots.size()) continue;
        const auto &slot = map.raidLootSlots[loot.slotIndex];
        // Preserve the first roll of every route (including guaranteed sources).
        if (!routes.insert(slot.route).second) budget.push_back(&loot);
    }
    if (budget.size() < 2U) return {false, "armed encounter lacks ordinary budget"};
    for (std::size_t i{}; i < kSources.size(); ++i)
    {
        auto &enemy = raid.enemies[i];
        auto &loot = *budget[i];
        const auto position = post(map, raid, i, enemy.size);
        if (!position || !profile.assets.erase(loot.assetId)) return {false, "armed encounter lacks post or loot"};
        enemy.position = *position;
        enemy.maximumHealth = 24;
        enemy.sourceEnemyId = kSources[i];
        const auto root = profile.assets.create(content.item(kContainer), RaidCorpseContainerAssetLocation{
            raid.raidId, outdoorRaidSpaceId(), enemy.sourceEnemyId, std::nullopt, RaidCorpseSourceKind::ArmedScavenger});
        const auto container = ProfileContainerId::compartment(root, 0);
        const auto pistol = profile.assets.create(content.item(kPistol), StoredAssetLocation{container, {0, 0}});
        const auto first = profile.assets.create(content.item(kMagazine), InstalledMagazineLocation{pistol});
        const auto fit = findFirstProfileFit(profile, content, container, content.item(kMagazine), ItemOrientation::Degrees0);
        if (!fit) return {false, "armed encounter magazine cannot fit"};
        const auto second = profile.assets.create(content.item(kMagazine), StoredAssetLocation{container, *fit});
        const MagazineRoundRecord round{kRound, std::nullopt};
        profile.assets.findMutable(pistol)->chamberedRound = round;
        profile.assets.findMutable(first)->magazineRounds.assign(5, round);
        profile.assets.findMutable(second)->magazineRounds.assign(6, round);
        loot.assetId = pistol;
        loot.definitionId = kPistol;
        loot.quantity = 1;
        loot.corpseContainerAssetId = root;
        raid.armedScavengers.push_back({enemy.sourceEnemyId, root, pistol, first, second});
    }
    return {true, {}};
}

ProfileValidationResult validateRaidArmedScavengers(const ProfileState &profile, const ContentRegistry &content)
{
    if (!profile.pendingRaid || profile.pendingRaid->armedScavengers.empty()) return {true, {}};
    const auto &raid = *profile.pendingRaid;
    if (!supportsRiversideArmedScavengersContent(content.contentVersion()) || !ordinaryRiverside(raid) ||
        raid.armedScavengers.size() != 2U || raid.enemies.size() < 2U || !raid.startingAssets)
        return {false, "invalid armed encounter"};
    const auto &map = content.map(raid.mapDefinitionId);
    std::set<AssetInstanceId> assigned;
    for (std::size_t i{}; i < kSources.size(); ++i)
    {
        const auto &actor = raid.armedScavengers[i];
        const auto &enemy = raid.enemies[i];
        const auto position = post(map, raid, i, enemy.size);
        if (actor.retreatPoint.has_value() != raid.armedScavengers.front().retreatPoint.has_value() ||
            (actor.retreatPoint && (!supportsRaidScavengerRetreatContent(content.contentVersion()) ||
                !validRaidScavengerRetreatPoint(map, raid, actor, *actor.retreatPoint))))
            return {false, "invalid armed scavenger retreat point"};
        if (actor.sourceEnemyId != kSources[i] || enemy.sourceEnemyId != actor.sourceEnemyId ||
            enemy.spaceId != outdoorRaidSpaceId() || enemy.maximumHealth != 24 || enemy.variant ||
            enemy.torsoArmor || !enemy.encounterGroupInstanceId.empty() || !position ||
            position->x != enemy.position.x || position->y != enemy.position.y) return {false, "invalid armed post"};
        for (auto id : {actor.corpseAssetId, actor.pistolAssetId, actor.firstMagazineId, actor.secondMagazineId})
            if (id < raid.startingAssets->nextAssetId() || id >= profile.assets.nextAssetId() || !assigned.insert(id).second)
                return {false, "invalid armed equipment identity"};
        const auto *root = profile.assets.find(actor.corpseAssetId);
        const auto *corpse = root ? std::get_if<RaidCorpseContainerAssetLocation>(&root->location) : nullptr;
        if (!corpse || corpse->sourceKind != RaidCorpseSourceKind::ArmedScavenger ||
            root->definitionId != kContainer || corpse->sourceEnemyId != actor.sourceEnemyId ||
            corpse->raidId != raid.raidId || corpse->spaceId != outdoorRaidSpaceId()) return {false, "invalid armed corpse"};
        if (actor.escaped && (!actor.retreatPoint || corpse->deathPosition))
            return {false, "an escaped scavenger must retain an unopened loadout"};
        for (const auto id : {actor.firstMagazineId, actor.secondMagazineId})
            if (const auto *magazine = profile.assets.find(id); magazine && magazine->definitionId != kMagazine)
                return {false, "armed magazine identity changed"};
        if (!corpse->deathPosition)
        {
            for (const auto &[id, item] : profile.assets.records())
                if (storedIn(item, actor.corpseAssetId) && id != actor.pistolAssetId &&
                    id != actor.firstMagazineId && id != actor.secondMagazineId)
                    return {false, "unrevealed armed corpse contains an unrelated item"};
        }
        const auto loot = std::find_if(raid.loot.begin(), raid.loot.end(),
            [&](const auto &entry) { return entry.assetId == actor.pistolAssetId; });
        if (loot == raid.loot.end() || loot->corpseContainerAssetId != actor.corpseAssetId ||
            loot->definitionId != kPistol || loot->quantity != 1 || loot->requiresHighRisk ||
            loot->sceneContainerAssetId || loot->spaceId != outdoorRaidSpaceId() ||
            (!loot->collected && raidSceneContainerRoot(profile, loot->assetId) != actor.corpseAssetId) ||
            (!corpse->deathPosition && (loot->collected || !validHeldEquipment(profile.assets, actor))))
            return {false, "invalid armed encounter equipment or budget"};
    }
    return {true, {}};
}

bool raidArmedScavengerEscaped(const PendingRaidSnapshot &raid, std::string_view sourceEnemyId) noexcept
{
    return std::any_of(raid.armedScavengers.begin(), raid.armedScavengers.end(), [&](const auto &actor) {
        return actor.escaped && actor.sourceEnemyId == sourceEnemyId;
    });
}

bool sealRaidArmedScavengerEscape(ProfileState &profile, const ContentRegistry &content,
                                  std::string_view sourceEnemyId, Vec2 actorCenter)
{
    if (!profile.pendingRaid)
        return false;
    auto &roster = profile.pendingRaid->armedScavengers;
    const auto actor = std::find_if(roster.begin(), roster.end(), [&](const auto &entry) {
        return entry.sourceEnemyId == sourceEnemyId;
    });
    if (actor == roster.end() || !actor->retreatPoint || !validateRaidArmedScavengers(profile, content).valid)
        return false;
    const float dx = actorCenter.x - actor->retreatPoint->x;
    const float dy = actorCenter.y - actor->retreatPoint->y;
    if (!std::isfinite(dx) || !std::isfinite(dy) || dx * dx + dy * dy >
        kRaidScavengerRetreatExitRadius * kRaidScavengerRetreatExitRadius)
        return false;
    if (actor->escaped)
        return true;
    const auto &root = std::get<RaidCorpseContainerAssetLocation>(profile.assets.find(actor->corpseAssetId)->location);
    if (root.deathPosition || profile.revision == std::numeric_limits<ProfileRevision>::max())
        return false;
    // Keep the frozen budget and its actual firearm together. Settlement already
    // removes an unclaimed corpse tree, including its installed magazine and rounds.
    actor->escaped = true;
    ++profile.revision;
    return true;
}

bool syncRaidArmedScavengerAmmo(ProfileState &profile, const ContentRegistry &, std::string_view sourceEnemyId,
                                int shotsFired, bool reloaded)
{
    if (!profile.pendingRaid || shotsFired < (reloaded ? 6 : 0) || shotsFired > (reloaded ? 12 : 6) ||
        profile.revision == std::numeric_limits<ProfileRevision>::max()) return false;
    for (const auto &actor : profile.pendingRaid->armedScavengers)
    {
        if (actor.sourceEnemyId != sourceEnemyId) continue;
        if (actor.escaped) return false;
        const auto *root = profile.assets.find(actor.corpseAssetId);
        const auto *corpse = root ? std::get_if<RaidCorpseContainerAssetLocation>(&root->location) : nullptr;
        if (!corpse || corpse->deathPosition || !validHeldEquipment(profile.assets, actor)) return false;
        auto &gun = *profile.assets.findMutable(actor.pistolAssetId);
        auto &first = *profile.assets.findMutable(actor.firstMagazineId);
        auto &second = *profile.assets.findMutable(actor.secondMagazineId);
        const bool swapped = installedIn(second, gun.instanceId);
        const int currentShots = 12 - static_cast<int>(first.magazineRounds.size() + second.magazineRounds.size()) -
            static_cast<int>(gun.chamberedRound.has_value());
        if (shotsFired < currentShots || (swapped && !reloaded) || (swapped == reloaded && shotsFired == currentShots))
            return false;
        if (!swapped && reloaded)
        {
            // Account for the last shots from the first magazine, then feed the original spare.
            auto chamber = second.magazineRounds.back();
            first.location = second.location;
            second.location = InstalledMagazineLocation{gun.instanceId};
            first.magazineRounds.clear();
            gun.chamberedRound = std::move(chamber);
            second.magazineRounds.pop_back();
        }
        auto &active = reloaded ? second : first;
        const int remaining = (reloaded ? 12 : 6) - shotsFired;
        active.magazineRounds.resize(static_cast<std::size_t>(std::max(0, remaining - 1)));
        if (!remaining) gun.chamberedRound.reset();
        ++profile.revision;
        return true;
    }
    return false;
}
