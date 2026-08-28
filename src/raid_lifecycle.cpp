#include "raid_lifecycle.h"

#include "base_construction_domain.h"
#include "base_manufacturing_domain.h"
#include "base_morale_domain.h"
#include "base_siege_domain.h"

#include <algorithm>
#include <limits>
#include <numeric>
#include <set>

#include "base_resource_domain.h"
#include "regional_operations_domain.h"
#include "base_population_domain.h"
#include "base_resident_medical_domain.h"
#include "lost_raid_domain.h"
#include "raid_map_generation.h"
#include "stable_random.h"

namespace
{
DeployReceipt deployFailure(
    RaidLifecycleError error,
    std::string message,
    ProfileRevision revision)
{
    return {false, false, error, std::move(message), revision};
}

RaidSettlementReceipt settlementFailure(
    RaidLifecycleError error,
    std::string message,
    ProfileRevision revision,
    RaidResultOutcome outcome)
{
    return {false, false, error, std::move(message), revision, outcome};
}

const LootContentEntry &rollLoot(
    const LootTableDefinition &table,
    Pcg32 &random)
{
    std::uint64_t total{};
    for (const LootContentEntry &entry : table.entries)
    {
        total += entry.weight;
    }
    std::uint64_t value = random.bounded(static_cast<std::uint32_t>(total));
    for (const LootContentEntry &entry : table.entries)
    {
        if (value < entry.weight)
        {
            return entry;
        }
        value -= entry.weight;
    }
    return table.entries.back();
}

std::vector<std::size_t> selectLootSlots(
    const MapDefinition &map,
    Pcg32 &random)
{
    std::vector<std::size_t> selected;
    std::set<std::size_t> used;
    for (const std::string_view route : {"central", "perimeter", "resource"})
    {
        std::vector<std::size_t> candidates;
        for (std::size_t index = 0; index < map.raidLootSlots.size(); ++index)
        {
            if (map.raidLootSlots[index].route == route)
            {
                candidates.push_back(index);
            }
        }
        const std::size_t selectedIndex = candidates[
            random.bounded(static_cast<std::uint32_t>(candidates.size()))];
        selected.push_back(selectedIndex);
        used.insert(selectedIndex);
    }

    const std::size_t target = 6U + random.bounded(4U);
    while (selected.size() < target)
    {
        const std::size_t candidate = random.bounded(
            static_cast<std::uint32_t>(map.raidLootSlots.size()));
        if (used.insert(candidate).second)
        {
            selected.push_back(candidate);
        }
    }
    std::sort(selected.begin(), selected.end());
    return selected;
}

std::vector<ProfileContainerId> carriedContainers(
    const ProfileState &profile,
    const ContentRegistry &content,
    EquipmentSlotKind slot)
{
    std::vector<ProfileContainerId> result;
    const auto owner = equippedAsset(profile, slot);
    if (!owner.has_value())
    {
        return result;
    }
    const AssetRecord *asset = profile.assets.find(*owner);
    if (asset == nullptr)
    {
        return result;
    }
    const auto &definition = content.item(asset->definitionId);
    for (std::size_t index = 0;
         index < definition.containerCompartments.size();
         ++index)
    {
        result.push_back(ProfileContainerId::compartment(
            *owner,
            static_cast<std::uint32_t>(index)));
    }
    return result;
}

bool hasStoredChildren(
    const ProfileState &profile,
    AssetInstanceId ownerId) noexcept
{
    for (const auto &[id, asset] : profile.assets.records())
    {
        static_cast<void>(id);
        const auto *stored = std::get_if<StoredAssetLocation>(&asset.location);
        if (stored != nullptr &&
            stored->container.kind == ProfileContainerKind::AssetCompartment &&
            stored->container.ownerAssetId == ownerId)
        {
            return true;
        }
    }
    return false;
}

std::set<AssetInstanceId> assetTreeIds(
    const ProfileState &profile,
    AssetInstanceId rootId)
{
    std::set<AssetInstanceId> result{rootId};
    bool changed = true;
    while (changed)
    {
        changed = false;
        for (const auto &[assetId, asset] : profile.assets.records())
        {
            bool child{};
            if (const auto *stored =
                    std::get_if<StoredAssetLocation>(&asset.location))
            {
                child = stored->container.kind ==
                        ProfileContainerKind::AssetCompartment &&
                    result.contains(stored->container.ownerAssetId);
            }
            else if (const auto *installed =
                         std::get_if<InstalledMagazineLocation>(
                             &asset.location))
            {
                child = result.contains(installed->weaponAssetId);
            }
            if (child && result.insert(assetId).second)
            {
                changed = true;
            }
        }
    }
    return result;
}

// Abnormal Raid rollback removes exactly the generated quantity even when a
// picked stack was merged into a pre-Raid carried stack. Successful extraction
// never calls this helper and preserves the merged stack in place.
bool consumeCollectedLoot(
    ProfileState &profile,
    const RaidLootSnapshot &loot)
{
    std::vector<AssetInstanceId> sources;
    if (const AssetRecord *original = profile.assets.find(loot.assetId);
        original != nullptr && assetIsCarried(profile, loot.assetId) &&
        original->definitionId == loot.definitionId &&
        !original->reliefBatchId.has_value() &&
        !hasStoredChildren(profile, loot.assetId))
    {
        sources.push_back(loot.assetId);
    }
    for (const auto &[id, asset] : profile.assets.records())
    {
        if (id == loot.assetId || !assetIsCarried(profile, id) ||
            asset.definitionId != loot.definitionId ||
            asset.reliefBatchId.has_value() || hasStoredChildren(profile, id))
        {
            continue;
        }
        sources.push_back(id);
    }

    std::uint64_t available{};
    for (AssetInstanceId id : sources)
    {
        available += profile.assets.find(id)->quantity;
    }
    if (available < loot.quantity)
    {
        return false;
    }

    std::uint32_t remaining = loot.quantity;
    for (AssetInstanceId id : sources)
    {
        if (remaining == 0)
        {
            break;
        }
        AssetRecord *asset = profile.assets.findMutable(id);
        const std::uint32_t consumed = std::min(remaining, asset->quantity);
        asset->quantity -= consumed;
        remaining -= consumed;
        if (asset->quantity == 0)
        {
            static_cast<void>(profile.assets.erase(id));
        }
    }
    return true;
}

bool advanceProfileWorldTime(
    ProfileState &profile,
    const ContentRegistry &content,
    std::uint32_t minutes) noexcept
{
    const WorldClockAdvanceResult advanced =
        advanceWorldClock(profile.worldClock, minutes);
    if (advanced.minutesApplied != minutes)
    {
        return false;
    }
    static_cast<void>(synchronizeBaseDailySystemsThrough(profile, content));
    static_cast<void>(applyBaseConstructionThrough(profile, content));
    static_cast<void>(applyBaseManufacturingThrough(profile, content));
    static_cast<void>(applyResidentTreatmentThrough(profile));
    return true;
}

void ageLostRaidRecords(ProfileState &profile)
{
    std::vector<std::string> expiredRecordIds;
    for (auto &[recordId, record] : profile.lostRaidRecords)
    {
        if (record.subsequentRaidSettlementCount >=
            kLostRaidRecordRetainedSettlementCount)
        {
            expiredRecordIds.push_back(recordId);
        }
        else
        {
            ++record.subsequentRaidSettlementCount;
        }
    }

    for (const std::string &recordId : expiredRecordIds)
    {
        std::vector<AssetInstanceId> expiredAssets;
        for (const auto &[assetId, asset] : profile.assets.records())
        {
            static_cast<void>(asset);
            const std::optional<std::string> owner =
                lostRaidRecordForAsset(profile, assetId);
            if (owner.has_value() && *owner == recordId)
            {
                expiredAssets.push_back(assetId);
            }
        }
        for (AssetInstanceId assetId : expiredAssets)
        {
            static_cast<void>(profile.assets.erase(assetId));
        }
        profile.lostRaidRecords.erase(recordId);
    }
}

std::optional<std::string> createLostRaidRecord(
    ProfileState &profile,
    const ContentRegistry &content,
    const PendingRaidSnapshot &raid,
    RaidResultOutcome outcome)
{
    std::vector<std::pair<AssetInstanceId, EquipmentSlotKind>> roots;
    for (const auto &[assetId, asset] : profile.assets.records())
    {
        if (const auto *equipped =
                std::get_if<EquippedAssetLocation>(&asset.location))
        {
            roots.emplace_back(assetId, equipped->slot);
        }
    }
    if (roots.empty())
    {
        return std::nullopt;
    }

    const MapDefinition &map = content.map(raid.mapDefinitionId);
    LostRaidRecord record{
        raid.settlementId,
        raid.raidId,
        raid.settlementId,
        raid.mapDefinitionId,
        map.operationBriefing.difficulty,
        outcome,
        profile.worldClock.elapsedWorldMinutes,
        0U};
    if (!profile.lostRaidRecords.emplace(record.recordId, record).second)
    {
        return std::nullopt;
    }
    for (const auto &[assetId, slot] : roots)
    {
        profile.assets.findMutable(assetId)->location =
            LostRaidAssetLocation{record.recordId, slot};
    }
    return record.recordId;
}
}

RaidTravelPreview queryRaidTravel(
    const ProfileState &profile,
    const ContentRegistry &content,
    const MapDefinition &map) noexcept
{
    const RegionalRoutePlan route = queryRegionalRoute(
        profile, content, map.id);
    if (!route.reachable || route.travelMinutes == 0U ||
        route.travelMinutes > std::numeric_limits<std::uint32_t>::max() / 2U)
    {
        return {};
    }
    const std::uint32_t returnMinutes = route.travelMinutes;
    const std::uint32_t failureMinutes = route.travelMinutes * 2U;
    WorldClockState arrivalClock = profile.worldClock;
    static_cast<void>(advanceWorldClock(
        arrivalClock,
        route.travelMinutes));
    WorldClockState extractedClock = arrivalClock;
    static_cast<void>(advanceWorldClock(
        extractedClock,
        returnMinutes));
    WorldClockState failureClock = arrivalClock;
    static_cast<void>(advanceWorldClock(
        failureClock,
        failureMinutes));
    return RaidTravelPreview{
        true,
        route.travelMinutes,
        returnMinutes,
        failureMinutes,
        projectWorldClock(profile.worldClock),
        projectWorldClock(arrivalClock),
        projectWorldClock(extractedClock),
        projectWorldClock(failureClock),
        route.routeIds,
        route.usesOnlineOutpost};
}

DeployReceipt executeDeploy(
    ProfileState &profile,
    const ContentRegistry &content,
    const DeployCommand &command,
    const CommandContext &context)
{
    if (context.transactionId.empty() || command.raidId.empty() ||
        command.settlementId.empty() || command.seed == 0 ||
        command.mapDefinitionId.value().empty())
    {
        return deployFailure(
            RaidLifecycleError::InvalidCommand,
            "Deploy command is invalid",
            profile.revision);
    }
    if (profile.committedTransactions.contains(context.transactionId))
    {
        return {true, true, RaidLifecycleError::None, {}, profile.revision};
    }
    if (context.expectedRevision != profile.revision)
    {
        return deployFailure(
            RaidLifecycleError::StaleRevision,
            "profile revision is stale",
            profile.revision);
    }
    if (profile.pendingRaid.has_value())
    {
        return deployFailure(
            RaidLifecycleError::RaidAlreadyPending,
            "a Raid is already pending",
            profile.revision);
    }
    if (profile.baseSiege.warningActive)
    {
        return deployFailure(
            RaidLifecycleError::InvalidCommand,
            "resolve the Base siege warning before deploying",
            profile.revision);
    }
    if (profile.revision == std::numeric_limits<ProfileRevision>::max())
    {
        return deployFailure(
            RaidLifecycleError::RevisionOverflow,
            "profile revision cannot advance",
            profile.revision);
    }

    ProfileState candidate = profile;
    const MapDefinition *map{};
    try
    {
        map = &content.map(command.mapDefinitionId);
    }
    catch (...)
    {
        return deployFailure(
            RaidLifecycleError::InvalidCommand,
            "Deploy map does not exist",
            profile.revision);
    }
    if (map->spawnExtractionPairs.size() != 3 ||
        map->raidEnemyDeploymentIds.size() != 3 ||
        map->raidLootSlots.size() < 9)
    {
        return deployFailure(
            RaidLifecycleError::InvalidCommand,
            "Deploy map has no validated Alpha configuration",
            profile.revision);
    }

    const std::size_t regionalMissionCount =
        static_cast<std::size_t>(command.selfRecoveryRecordId.has_value()) +
        static_cast<std::size_t>(command.outpostRestorationId.has_value()) +
        static_cast<std::size_t>(command.baseSiteClearanceId.has_value()) +
        static_cast<std::size_t>(command.basePerimeterSweepId.has_value());
    if (regionalMissionCount > 1U)
    {
        return deployFailure(
            RaidLifecycleError::InvalidCommand,
            "regional mission modes cannot be combined",
            profile.revision);
    }

    std::optional<RegionalOutpostRestorationSnapshot> outpostRestoration;
    if (command.outpostRestorationId.has_value())
    {
        try
        {
            const RegionalOutpostDefinition &definition =
                content.regionalOutpost(*command.outpostRestorationId);
            const auto state = candidate.regionalOperations.outposts.find(
                definition.id);
            if (definition.restorationMapDefinitionId !=
                    command.mapDefinitionId ||
                state == candidate.regionalOperations.outposts.end() ||
                !state->second.unlocked || !state->second.established ||
                !state->second.disrupted ||
                assignedRegionalOutpostStaff(state->second) !=
                    definition.requiredStaff)
            {
                return deployFailure(
                    RaidLifecycleError::InvalidCommand,
                    "outpost restoration deployment is unavailable",
                    profile.revision);
            }
            outpostRestoration = RegionalOutpostRestorationSnapshot{
                definition.id,
                false};
        }
        catch (...)
        {
            return deployFailure(
                RaidLifecycleError::InvalidCommand,
                "outpost restoration definition does not exist",
                profile.revision);
        }
    }
    std::optional<RegionalBaseSiteClearanceSnapshot> baseSiteClearance;
    if (command.baseSiteClearanceId.has_value())
    {
        const RegionalBaseSiteClearancePlan plan =
            queryRegionalBaseSiteClearance(
                candidate, content, *command.baseSiteClearanceId);
        if (!plan.canDeploy || plan.mapDefinitionId != command.mapDefinitionId)
        {
            return deployFailure(
                RaidLifecycleError::InvalidCommand,
                plan.message.empty()
                    ? "Base site clearance deployment is unavailable"
                    : plan.message,
                profile.revision);
        }
        baseSiteClearance = RegionalBaseSiteClearanceSnapshot{
            *command.baseSiteClearanceId,
            false};
    }
    std::optional<BasePerimeterSweepSnapshot> basePerimeterSweep;
    if (command.basePerimeterSweepId.has_value())
    {
        const BasePerimeterSweepPlan plan = queryBasePerimeterSweep(
            candidate, content);
        if (!plan.canDeploy ||
            plan.baseSiteDefinitionId != *command.basePerimeterSweepId ||
            plan.mapDefinitionId != command.mapDefinitionId)
        {
            return deployFailure(
                RaidLifecycleError::InvalidCommand,
                plan.message.empty()
                    ? "Base perimeter sweep deployment is unavailable"
                    : plan.message,
                profile.revision);
        }
        basePerimeterSweep = BasePerimeterSweepSnapshot{
            plan.baseSiteDefinitionId,
            plan.threatReductionUnits,
            false};
    }
    const RaidTravelPreview travel = queryRaidTravel(
        candidate, content, *map);
    if (!travel.reachable)
    {
        return deployFailure(
            RaidLifecycleError::InvalidCommand,
            "Deploy destination has no active regional route",
            profile.revision);
    }

    std::optional<LostRaidRecord> selfRecoveryRecord;
    std::vector<std::pair<AssetInstanceId, EquipmentSlotKind>>
        selfRecoveryRoots;
    if (command.selfRecoveryRecordId.has_value())
    {
        const auto record = candidate.lostRaidRecords.find(
            *command.selfRecoveryRecordId);
        if (command.selfRecoveryRecordId->empty() ||
            record == candidate.lostRaidRecords.end() ||
            record->second.mapDefinitionId != command.mapDefinitionId)
        {
            return deployFailure(
                RaidLifecycleError::InvalidCommand,
                "self-recovery record is unavailable on this map",
                profile.revision);
        }
        selfRecoveryRecord = record->second;
        for (const auto &[assetId, asset] : candidate.assets.records())
        {
            const auto *lost =
                std::get_if<LostRaidAssetLocation>(&asset.location);
            if (lost != nullptr && lost->recordId == record->first)
            {
                selfRecoveryRoots.emplace_back(assetId, lost->sourceSlot);
            }
        }
        if (selfRecoveryRoots.empty())
        {
            return deployFailure(
                RaidLifecycleError::InvalidProfile,
                "self-recovery record has no owned roots",
                profile.revision);
        }
    }

    for (std::size_t index = 0;
         index < kRaidIntelligenceCategoryCount;
         ++index)
    {
        if (!command.intelligence.selected[index])
        {
            continue;
        }
        const auto found = candidate.raidIntelligence.counts.find(
            command.mapDefinitionId);
        if (found == candidate.raidIntelligence.counts.end() ||
            found->second[index] == 0U)
        {
            return deployFailure(
                RaidLifecycleError::InsufficientIntelligence,
                "selected Raid intelligence is unavailable",
                profile.revision);
        }
    }

    Pcg32 configurationRandom{command.seed, 0x6d61702d636f6e66ULL};
    Pcg32 lootRandom{command.seed, 0x6c6f6f742d726169ULL};
    Pcg32 advancedLootRandom{command.seed, 0x686967682d6c6f6fULL};
    Pcg32 interiorLootRandom{command.seed, 0x696e746572696f72ULL};
    const std::size_t pairIndex = configurationRandom.bounded(3U);
    const std::size_t deploymentIndex = configurationRandom.bounded(3U);
    const auto &pair = map->spawnExtractionPairs[pairIndex];
    const auto &deployment = content.enemyDeployment(
        map->raidEnemyDeploymentIds[deploymentIndex]);

    PendingRaidSnapshot snapshot;
    snapshot.raidId = command.raidId;
    snapshot.settlementId = command.settlementId;
    snapshot.rulesVersion = "procedural-playable-outdoor-layout-21";
    snapshot.mapDefinitionId = command.mapDefinitionId;
    snapshot.seed = command.seed;
    snapshot.spawnExtractionPairId = pair.id;
    snapshot.enemyDeploymentId = deployment.id;
    snapshot.playerSpawn = pair.playerSpawn;
    snapshot.extractionPoint = pair.extractionPoint;
    snapshot.startingHealth = candidate.currentHealth;
    snapshot.startingMedicalStatus = candidate.medicalStatus;
    snapshot.intelligence = command.intelligence;
    snapshot.travel = RaidTravelSnapshot{
        travel.outboundMinutes,
        travel.returnMinutes,
        travel.failureRegroupMinutes,
        candidate.worldClock,
        candidate.baseResources,
        candidate.basePriority,
        candidate.baseMorale,
        candidate.baseCommunityEvent,
        candidate.baseConstruction,
        candidate.baseWorkforce,
        candidate.basePopulation.bedCapacity,
        candidate.basePopulation.injuredResidents,
        candidate.basePopulation.injuredByProfession,
        candidate.residentMedical,
        candidate.raidIntelligence};
    snapshot.travel.routeIds = travel.routeIds;
    snapshot.travel.startingRegionalOperations =
        candidate.regionalOperations;
    snapshot.travel.startingBaseSiege = candidate.baseSiege;
    snapshot.outpostRestoration = std::move(outpostRestoration);
    snapshot.baseSiteClearance = std::move(baseSiteClearance);
    snapshot.basePerimeterSweep = std::move(basePerimeterSweep);
    for (std::size_t index = 0;
         index < kRaidIntelligenceCategoryCount;
         ++index)
    {
        if (command.intelligence.selected[index])
        {
            --candidate.raidIntelligence.counts[command.mapDefinitionId][index];
        }
    }
    if (const auto found = candidate.raidIntelligence.counts.find(
            command.mapDefinitionId);
        found != candidate.raidIntelligence.counts.end() &&
        found->second == std::array<std::uint32_t,
                                    kRaidIntelligenceCategoryCount>{})
    {
        candidate.raidIntelligence.counts.erase(found);
    }
    if (map->rescue.has_value() &&
        !candidate.committedRescues.contains(map->rescue->id))
    {
        snapshot.rescue = RaidRescueSnapshot{
            map->rescue->id,
            map->rescue->subjectKind,
            map->rescue->transferPoint,
            map->rescue->interactionDurationSeconds,
            map->rescue->ordinaryResidentCount,
            map->rescue->injuredResidentCount,
            map->rescue->profession,
            false};
    }
    for (const EnemySpawnDefinition &enemy : deployment.enemies)
    {
        snapshot.enemies.push_back(
            RaidEnemySnapshot{enemy.position, enemy.size, enemy.maximumHealth});
    }
    for (std::size_t interiorIndex{};
         interiorIndex < map->interiors.size(); ++interiorIndex)
    {
        const RaidInteriorDefinition &interior =
            map->interiors[interiorIndex];
        RaidInteriorSnapshot frozen;
        frozen.id = interior.id;
        frozen.displayName = interior.displayName;
        frozen.layoutKnown =
            candidate.raidInteriorIntelligence.knows(interior.id);
        frozen.worldSize = interior.worldSize;
        frozen.exteriorEntrance = interior.exteriorEntrance;
        frozen.exteriorReturn = interior.exteriorReturn;
        frozen.interiorSpawn = interior.interiorSpawn;
        frozen.interiorExit = interior.interiorExit;
        frozen.ballisticBlockers.reserve(
            interior.ballisticBlockers.size());
        for (const BallisticBlockerDefinition &blocker :
             interior.ballisticBlockers)
        {
            frozen.ballisticBlockers.push_back(blocker.bounds);
        }
        snapshot.interiors.push_back(std::move(frozen));
        for (const EnemySpawnDefinition &enemy : interior.enemies)
        {
            snapshot.enemies.push_back(RaidEnemySnapshot{
                enemy.position,
                enemy.size,
                enemy.maximumHealth,
                interior.id});
        }
    }
    for (EquipmentSlotKind slot : {
             EquipmentSlotKind::PrimaryWeapon,
             EquipmentSlotKind::SecondaryWeapon,
             EquipmentSlotKind::Sidearm,
             EquipmentSlotKind::ChestRig,
             EquipmentSlotKind::Backpack,
             EquipmentSlotKind::Helmet,
             EquipmentSlotKind::BodyArmor})
    {
        if (const auto root = equippedAsset(candidate, slot))
        {
            snapshot.carriedRootAssetIds.push_back(*root);
        }
    }

    const LootTableDefinition &lootTable =
        content.lootTable(map->raidLootTableId);
    for (const std::size_t slotIndex : selectLootSlots(*map, lootRandom))
    {
        const LootContentEntry &entry = rollLoot(lootTable, lootRandom);
        const std::uint32_t range =
            entry.maximumQuantity - entry.minimumQuantity + 1U;
        const std::uint32_t quantity = entry.minimumQuantity +
            lootRandom.bounded(range);
        const AssetInstanceId assetId = candidate.assets.create(
            content.item(entry.itemDefinitionId),
            RaidGroundAssetLocation{
                command.raidId,
                static_cast<std::uint32_t>(slotIndex)},
            quantity);
        snapshot.loot.push_back(RaidLootSnapshot{
            assetId,
            entry.itemDefinitionId,
            quantity,
            static_cast<std::uint32_t>(slotIndex),
            map->raidLootSlots[slotIndex].position,
            false,
            false});
    }

    const LootTableDefinition &advancedLootTable =
        content.lootTable(map->highRisk.advancedLootTableId);
    for (std::size_t advancedIndex = 0U;
         advancedIndex < map->highRisk.advancedLootSlots.size();
         ++advancedIndex)
    {
        const LootContentEntry &entry =
            rollLoot(advancedLootTable, advancedLootRandom);
        const std::uint32_t range =
            entry.maximumQuantity - entry.minimumQuantity + 1U;
        const std::uint32_t quantity =
            entry.minimumQuantity + advancedLootRandom.bounded(range);
        const std::size_t slotIndex = map->raidLootSlots.size() + advancedIndex;
        const AssetInstanceId assetId = candidate.assets.create(
            content.item(entry.itemDefinitionId),
            RaidGroundAssetLocation{command.raidId,
                                    static_cast<std::uint32_t>(slotIndex)},
            quantity);
        snapshot.loot.push_back(RaidLootSnapshot{
            assetId,
            entry.itemDefinitionId,
            quantity,
            static_cast<std::uint32_t>(slotIndex),
            map->highRisk.advancedLootSlots[advancedIndex].position,
            true,
            false});
    }

    std::size_t interiorSlotIndex = map->raidLootSlots.size() +
        map->highRisk.advancedLootSlots.size();
    for (std::size_t interiorIndex{};
         interiorIndex < map->interiors.size(); ++interiorIndex)
    {
        const RaidInteriorDefinition &interior =
            map->interiors[interiorIndex];
        const LootTableDefinition &interiorLootTable =
            content.lootTable(interior.lootTableId);
        for (const RaidLootSlotDefinition &slot : interior.lootSlots)
        {
            const LootContentEntry &entry =
                rollLoot(interiorLootTable, interiorLootRandom);
            const std::uint32_t range =
                entry.maximumQuantity - entry.minimumQuantity + 1U;
            const std::uint32_t quantity = entry.minimumQuantity +
                interiorLootRandom.bounded(range);
            const AssetInstanceId assetId = candidate.assets.create(
                content.item(entry.itemDefinitionId),
                RaidGroundAssetLocation{
                    command.raidId,
                    static_cast<std::uint32_t>(interiorSlotIndex)},
                quantity);
            snapshot.loot.push_back(RaidLootSnapshot{
                assetId,
                entry.itemDefinitionId,
                quantity,
                static_cast<std::uint32_t>(interiorSlotIndex),
                slot.position,
                false,
                false,
                interior.id});
            ++interiorSlotIndex;
        }
    }

    if (selfRecoveryRecord.has_value())
    {
        std::set<std::size_t> occupiedRegularSlots;
        for (const RaidLootSnapshot &loot : snapshot.loot)
        {
            if (loot.spaceId == outdoorRaidSpaceId() &&
                !loot.requiresHighRisk &&
                loot.slotIndex < map->raidLootSlots.size())
            {
                occupiedRegularSlots.insert(loot.slotIndex);
            }
        }
        if (occupiedRegularSlots.size() == map->raidLootSlots.size())
        {
            const auto removed = std::find_if(
                snapshot.loot.rbegin(), snapshot.loot.rend(),
                [&](const RaidLootSnapshot &loot)
                {
                    return loot.spaceId == outdoorRaidSpaceId() &&
                        !loot.requiresHighRisk &&
                        loot.slotIndex < map->raidLootSlots.size();
                });
            if (removed == snapshot.loot.rend())
            {
                return deployFailure(
                    RaidLifecycleError::InvalidCommand,
                    "self-recovery cache has no legal map location",
                    profile.revision);
            }
            const std::size_t freed = removed->slotIndex;
            static_cast<void>(candidate.assets.erase(removed->assetId));
            snapshot.loot.erase(std::next(removed).base());
            occupiedRegularSlots.erase(freed);
        }

        std::vector<std::size_t> candidates;
        for (std::size_t index{}; index < map->raidLootSlots.size(); ++index)
        {
            if (!occupiedRegularSlots.contains(index))
            {
                candidates.push_back(index);
            }
        }
        if (candidates.empty())
        {
            return deployFailure(
                RaidLifecycleError::InvalidCommand,
                "self-recovery cache has no legal map location",
                profile.revision);
        }
        Pcg32 recoveryRandom{command.seed, 0x7265636f76657279ULL};
        const std::size_t cacheSlot = candidates[
            recoveryRandom.bounded(
                static_cast<std::uint32_t>(candidates.size()))];
        const Vec2 cachePosition = map->raidLootSlots[cacheSlot].position;
        RaidSelfRecoverySnapshot recovery;
        recovery.sourceRecord = *selfRecoveryRecord;
        recovery.cachePosition = cachePosition;
        const std::uint32_t firstSyntheticSlot = static_cast<std::uint32_t>(
            map->raidLootSlots.size() +
            map->highRisk.advancedLootSlots.size() +
            std::accumulate(
                map->interiors.begin(), map->interiors.end(), std::size_t{},
                [](std::size_t total, const RaidInteriorDefinition &interior)
                { return total + interior.lootSlots.size(); }));
        for (std::size_t index{}; index < selfRecoveryRoots.size(); ++index)
        {
            const float column = static_cast<float>(index % 3U) - 1.0F;
            const float row = static_cast<float>(index / 3U);
            recovery.roots.push_back(RaidSelfRecoveryRootSnapshot{
                selfRecoveryRoots[index].first,
                selfRecoveryRoots[index].second,
                firstSyntheticSlot + static_cast<std::uint32_t>(index),
                Vec2{cachePosition.x + column * 32.0F,
                     cachePosition.y + row * 32.0F}});
        }
        snapshot.selfRecovery = std::move(recovery);
    }

    RaidMapGenerationAnchors generationAnchors;
    generationAnchors.playerSpawn = snapshot.playerSpawn;
    generationAnchors.extractionPoint = snapshot.extractionPoint;
    generationAnchors.occupiedRegions = {
        map->highRisk.emergencyExtractionPoint,
        map->highRisk.conditionalExtractionPoint,
        map->highRisk.activationControlPoint,
        map->highRisk.advancedResourceArea};
    for (const RaidEnemySnapshot &enemy : snapshot.enemies)
    {
        if (enemy.spaceId != outdoorRaidSpaceId())
        {
            continue;
        }
        generationAnchors.occupiedRegions.push_back(
            ContentRect{enemy.position, enemy.size});
        generationAnchors.reachablePoints.push_back(
            Vec2{enemy.position.x + enemy.size.x * 0.5F,
                 enemy.position.y + enemy.size.y * 0.5F});
    }
    for (const RaidLootSnapshot &loot : snapshot.loot)
    {
        if (loot.spaceId != outdoorRaidSpaceId())
        {
            continue;
        }
        generationAnchors.reachablePoints.push_back(loot.position);
    }
    if (snapshot.selfRecovery.has_value())
    {
        generationAnchors.reachablePoints.push_back(
            snapshot.selfRecovery->cachePosition);
    }
    for (const EnemySpawnDefinition &spawn : map->highRisk.pressureSpawns)
    {
        generationAnchors.occupiedRegions.push_back(
            ContentRect{spawn.position, spawn.size});
    }
    const auto addReachableRegion = [&generationAnchors](ContentRect region)
    {
        generationAnchors.reachablePoints.push_back(
            Vec2{region.position.x + region.size.x * 0.5F,
                 region.position.y + region.size.y * 0.5F});
    };
    addReachableRegion(map->highRisk.activationControlPoint);
    addReachableRegion(map->highRisk.advancedResourceArea);
    addReachableRegion(map->highRisk.emergencyExtractionPoint);
    addReachableRegion(map->highRisk.conditionalExtractionPoint);
    if (snapshot.rescue.has_value())
    {
        generationAnchors.occupiedRegions.push_back(
            snapshot.rescue->transferPoint);
        addReachableRegion(snapshot.rescue->transferPoint);
    }
    for (std::size_t interiorIndex{};
         interiorIndex < map->interiors.size(); ++interiorIndex)
    {
        const RaidExteriorPlacementDefinition *placement =
            selectRaidExteriorPlacement(
                map->interiors[interiorIndex],
                snapshot.seed,
                interiorIndex,
                generationAnchors);
        if (placement == nullptr)
        {
            return deployFailure(
                RaidLifecycleError::InvalidCommand,
                "Raid special location has no legal exterior placement",
                profile.revision);
        }
        RaidInteriorSnapshot &interior = snapshot.interiors[interiorIndex];
        interior.exteriorEntrance = placement->entrance;
        interior.exteriorReturn = placement->returnPoint;
        appendRaidExteriorPlacementAnchors(generationAnchors, *placement);
    }
    snapshot.spatialLayout = generateRaidMapLayout(
        *map,
        snapshot.seed,
        generationAnchors);

    candidate.pendingRaid = std::move(snapshot);
    if (!advanceProfileWorldTime(
            candidate,
            content,
            candidate.pendingRaid->travel.outboundMinutes))
    {
        return deployFailure(
            RaidLifecycleError::RevisionOverflow,
            "world clock cannot apply outbound travel",
            profile.revision);
    }
    candidate.lastRaidResult.reset();
    candidate.committedTransactions.insert(context.transactionId);
    ++candidate.revision;
    const ProfileValidationResult validation =
        validateProfileState(candidate, content);
    if (!validation.valid)
    {
        return deployFailure(
            RaidLifecycleError::InvalidProfile,
            validation.message,
            profile.revision);
    }
    profile = std::move(candidate);
    return {true, false, RaidLifecycleError::None, {}, profile.revision};
}

RaidSettlementReceipt settlePendingRaid(
    ProfileState &profile,
    const ContentRegistry &content,
    std::string_view settlementId,
    RaidResultOutcome outcome)
{
    const std::string id{settlementId};
    if (id.empty())
    {
        return settlementFailure(
            RaidLifecycleError::InvalidCommand,
            "Settlement ID is empty",
            profile.revision,
            outcome);
    }
    if (outcome == RaidResultOutcome::AbnormalQuit)
    {
        return settlementFailure(
            RaidLifecycleError::InvalidCommand,
            "abnormal Raid exit must restore the pre-Raid profile",
            profile.revision,
            outcome);
    }
    if (profile.committedSettlements.contains(id))
    {
        return {true, true, RaidLifecycleError::None, {},
                profile.revision, outcome};
    }
    if (!profile.pendingRaid.has_value() ||
        profile.pendingRaid->settlementId != id)
    {
        return settlementFailure(
            RaidLifecycleError::MissingPendingRaid,
            "pending Raid does not match Settlement ID",
            profile.revision,
            outcome);
    }
    if (profile.revision == std::numeric_limits<ProfileRevision>::max())
    {
        return settlementFailure(
            RaidLifecycleError::RevisionOverflow,
            "profile revision cannot advance",
            profile.revision,
            outcome);
    }

    ProfileState candidate = profile;
    const PendingRaidSnapshot raidSnapshot = *candidate.pendingRaid;
    std::vector<ItemDefinitionId> returned;
    if (outcome == RaidResultOutcome::Extracted)
    {
        for (const auto &[assetId, asset] : candidate.assets.records())
        {
            static_cast<void>(assetId);
            if (assetIsCarried(candidate, asset.instanceId))
            {
                returned.push_back(asset.definitionId);
            }
        }
        // Successful extraction confirms ownership but never rewrites the
        // location of a carried asset. Loot therefore stays in the exact
        // equipment slot or container cell chosen during the Raid. Future
        // carried roots (for example a vehicle cargo hold) must follow the
        // same location-preserving settlement rule.
    }

    std::set<AssetInstanceId> eraseIds;
    for (const auto &[assetId, asset] : candidate.assets.records())
    {
        const bool onGround =
            std::holds_alternative<RaidGroundAssetLocation>(asset.location);
        if (onGround)
        {
            const std::set<AssetInstanceId> tree =
                assetTreeIds(candidate, assetId);
            eraseIds.insert(tree.begin(), tree.end());
        }
    }
    for (AssetInstanceId assetId : eraseIds)
    {
        static_cast<void>(candidate.assets.erase(assetId));
    }
    ageLostRaidRecords(candidate);
    applySettledRaidBaseThreat(candidate);
    const RegionalOutpostThreatAdvance outpostThreat =
        applySettledRegionalRouteUsage(
            candidate,
            content,
            raidSnapshot.travel.routeIds);
    if (!outpostThreat.succeeded)
    {
        return settlementFailure(
            RaidLifecycleError::InvalidProfile,
            outpostThreat.message,
            profile.revision,
            outcome);
    }
    if (outcome == RaidResultOutcome::Extracted &&
        raidSnapshot.outpostRestoration.has_value() &&
        raidSnapshot.outpostRestoration->objectiveSecured)
    {
        const auto state = candidate.regionalOperations.outposts.find(
            raidSnapshot.outpostRestoration->outpostDefinitionId);
        if (state == candidate.regionalOperations.outposts.end() ||
            !state->second.established || !state->second.disrupted)
        {
            return settlementFailure(
                RaidLifecycleError::InvalidProfile,
                "outpost restoration target is no longer disrupted",
                profile.revision,
                outcome);
        }
        state->second.disrupted = false;
        state->second.shortcutOperationsSinceRestoration = 0U;
    }
    if (outcome == RaidResultOutcome::Extracted &&
        raidSnapshot.baseSiteClearance.has_value() &&
        raidSnapshot.baseSiteClearance->objectiveSecured)
    {
        const RegionalBaseSiteDefinition &definition =
            content.regionalBaseSite(
                raidSnapshot.baseSiteClearance->baseSiteDefinitionId);
        const auto state = candidate.regionalOperations.baseSites.find(
            definition.id);
        if (state == candidate.regionalOperations.baseSites.end() ||
            state->second.unlocked ||
            !definition.outpostDefinitionId.has_value())
        {
            return settlementFailure(
                RaidLifecycleError::InvalidProfile,
                "Base site clearance target is no longer locked",
                profile.revision,
                outcome);
        }
        auto outpost = candidate.regionalOperations.outposts.find(
            *definition.outpostDefinitionId);
        if (outpost == candidate.regionalOperations.outposts.end() ||
            outpost->second.unlocked)
        {
            return settlementFailure(
                RaidLifecycleError::InvalidProfile,
                "Base site clearance outpost state is invalid",
                profile.revision,
                outcome);
        }
        state->second.unlocked = true;
        outpost->second.unlocked = true;
    }
    std::uint32_t baseThreatReducedUnits{};
    if (outcome == RaidResultOutcome::Extracted &&
        raidSnapshot.basePerimeterSweep.has_value() &&
        raidSnapshot.basePerimeterSweep->objectiveSecured)
    {
        const RegionalBaseSiteDefinition &definition =
            content.regionalBaseSite(
                raidSnapshot.basePerimeterSweep->baseSiteDefinitionId);
        if (definition.nodeId !=
                candidate.regionalOperations.activeBaseNodeId ||
            raidSnapshot.basePerimeterSweep->threatReductionUnits == 0U ||
            raidSnapshot.basePerimeterSweep->threatReductionUnits >
                kBaseSiegeThreatThreshold)
        {
            return settlementFailure(
                RaidLifecycleError::InvalidProfile,
                "Base perimeter sweep target is no longer valid",
                profile.revision,
                outcome);
        }
        baseThreatReducedUnits =
            applyBasePerimeterSweepThreatReduction(
                candidate,
                raidSnapshot.basePerimeterSweep->threatReductionUnits);
    }
    std::optional<std::string> lostRecordId;
    if (outcome != RaidResultOutcome::Extracted)
    {
        lostRecordId = createLostRaidRecord(
            candidate, content, raidSnapshot, outcome);
    }
    if (outcome != RaidResultOutcome::Extracted)
    {
        candidate.currentHealth = 100;
        candidate.medicalStatus = MedicalStatusState{};
    }
    const std::uint32_t travelMinutes =
        outcome == RaidResultOutcome::PlayerDead
            ? raidSnapshot.travel.failureRegroupMinutes
            : raidSnapshot.travel.returnMinutes;
    if (!advanceProfileWorldTime(candidate, content, travelMinutes))
    {
        return settlementFailure(
            RaidLifecycleError::RevisionOverflow,
            "world clock cannot apply Raid return travel",
            profile.revision,
            outcome);
    }
    if (lostRecordId.has_value())
    {
        candidate.lostRaidRecords.at(*lostRecordId).createdWorldMinute =
            candidate.worldClock.elapsedWorldMinutes;
    }
    candidate.pendingRaid.reset();
    static_cast<void>(applyBaseManufacturingThrough(candidate, content));
    candidate.committedSettlements.insert(id);
    candidate.lastRaidResult = LastRaidResult{
        id,
        outcome,
        std::move(returned),
        0,
        travelMinutes,
        raidSnapshot.rescue.has_value() && raidSnapshot.rescue->secured
            ? raidSnapshot.rescue->ordinaryResidentCount
            : 0U,
        raidSnapshot.rescue.has_value() && raidSnapshot.rescue->secured
            ? raidSnapshot.rescue->injuredResidentCount
            : 0U,
        lostRecordId,
        baseThreatReducedUnits};
    ++candidate.revision;
    const ProfileValidationResult validation =
        validateProfileState(candidate, content);
    if (!validation.valid)
    {
        return settlementFailure(
            RaidLifecycleError::InvalidProfile,
            validation.message,
            profile.revision,
            outcome);
    }
    profile = std::move(candidate);
    return {true, false, RaidLifecycleError::None, {},
            profile.revision, outcome};
}

RaidRollbackReceipt rollbackPendingRaidToBase(
    ProfileState &profile,
    const ContentRegistry &content)
{
    if (!profile.pendingRaid.has_value())
    {
        return {false, RaidLifecycleError::MissingPendingRaid,
                "no pending Raid can be rolled back", profile.revision};
    }
    if (profile.revision == std::numeric_limits<ProfileRevision>::max())
    {
        return {false, RaidLifecycleError::RevisionOverflow,
                "profile revision cannot advance", profile.revision};
    }

    ProfileState candidate = profile;
    std::set<AssetInstanceId> selfRecoveryAssetIds;
    if (candidate.pendingRaid->selfRecovery.has_value())
    {
        const RaidSelfRecoverySnapshot recovery =
            *candidate.pendingRaid->selfRecovery;
        for (const RaidSelfRecoveryRootSnapshot &root : recovery.roots)
        {
            selfRecoveryAssetIds.insert(root.assetId);
        }
        if (recovery.opened)
        {
            if (!candidate.lostRaidRecords.emplace(
                    recovery.sourceRecord.recordId,
                    recovery.sourceRecord).second)
            {
                return {false, RaidLifecycleError::InvalidProfile,
                        "self-recovery record cannot be restored",
                        profile.revision};
            }
            for (const RaidSelfRecoveryRootSnapshot &root : recovery.roots)
            {
                AssetRecord *asset = candidate.assets.findMutable(root.assetId);
                if (asset == nullptr)
                {
                    return {false, RaidLifecycleError::InvalidProfile,
                            "self-recovery asset cannot be restored",
                            profile.revision};
                }
                asset->location = LostRaidAssetLocation{
                    recovery.sourceRecord.recordId,
                    root.sourceSlot};
            }
        }
    }
    const int startingHealth = candidate.pendingRaid->startingHealth;
    const MedicalStatusState startingMedicalStatus =
        candidate.pendingRaid->startingMedicalStatus;
    const WorldClockState startingWorldClock =
        candidate.pendingRaid->travel.startingWorldClock;
    const BaseResourceState startingBaseResources =
        candidate.pendingRaid->travel.startingBaseResources;
    const BasePriorityState startingBasePriority =
        candidate.pendingRaid->travel.startingBasePriority;
    const BaseMoraleState startingBaseMorale =
        candidate.pendingRaid->travel.startingBaseMorale;
    const BaseCommunityEventState startingBaseCommunityEvent =
        candidate.pendingRaid->travel.startingBaseCommunityEvent;
    const BaseConstructionState startingBaseConstruction =
        candidate.pendingRaid->travel.startingBaseConstruction;
    const BaseWorkforceState startingBaseWorkforce =
        candidate.pendingRaid->travel.startingBaseWorkforce;
    const std::uint32_t startingBedCapacity =
        candidate.pendingRaid->travel.startingBedCapacity;
    const std::uint32_t startingInjuredResidents =
        candidate.pendingRaid->travel.startingInjuredResidents;
    const BaseProfessionCounts startingInjuredByProfession =
        candidate.pendingRaid->travel.startingInjuredByProfession;
    const BaseResidentMedicalState startingResidentMedical =
        candidate.pendingRaid->travel.startingResidentMedical;
    const RaidIntelligenceArchiveState startingRaidIntelligence =
        candidate.pendingRaid->travel.startingRaidIntelligence;
    const bool restoreRegionalOperations =
        candidate.pendingRaid->rulesVersion ==
            "regional-route-network-17" ||
        candidate.pendingRaid->rulesVersion ==
            "regional-outpost-restoration-18" ||
        candidate.pendingRaid->rulesVersion ==
            "regional-base-site-clearance-19" ||
        candidate.pendingRaid->rulesVersion ==
            "regional-base-perimeter-sweep-20" ||
        candidate.pendingRaid->rulesVersion ==
            "procedural-playable-outdoor-layout-21";
    const RegionalOperationsState startingRegionalOperations =
        candidate.pendingRaid->travel.startingRegionalOperations;
    const BaseSiegeState startingBaseSiege =
        candidate.pendingRaid->travel.startingBaseSiege;
    std::set<AssetInstanceId> generatedLoot;
    for (const RaidLootSnapshot &loot : candidate.pendingRaid->loot)
    {
        if (selfRecoveryAssetIds.contains(loot.assetId))
        {
            continue;
        }
        if (loot.collected)
        {
            if (!consumeCollectedLoot(candidate, loot))
            {
                return {false, RaidLifecycleError::InvalidProfile,
                        "collected Loot cannot be rolled back",
                        profile.revision};
            }
        }
        else
        {
            generatedLoot.insert(loot.assetId);
        }
    }
    for (AssetInstanceId assetId : generatedLoot)
    {
        static_cast<void>(candidate.assets.erase(assetId));
    }
    candidate.currentHealth = startingHealth;
    candidate.medicalStatus = startingMedicalStatus;
    candidate.worldClock = startingWorldClock;
    candidate.baseResources = startingBaseResources;
    candidate.basePriority = startingBasePriority;
    candidate.baseMorale = startingBaseMorale;
    candidate.baseCommunityEvent = startingBaseCommunityEvent;
    candidate.baseConstruction = startingBaseConstruction;
    candidate.baseWorkforce = startingBaseWorkforce;
    candidate.basePopulation.bedCapacity = startingBedCapacity;
    candidate.basePopulation.injuredResidents = startingInjuredResidents;
    candidate.basePopulation.injuredByProfession =
        startingInjuredByProfession;
    candidate.residentMedical = startingResidentMedical;
    candidate.raidIntelligence = startingRaidIntelligence;
    if (restoreRegionalOperations)
    {
        candidate.regionalOperations = startingRegionalOperations;
    }
    candidate.baseSiege = startingBaseSiege;
    candidate.pendingRaid.reset();
    candidate.lastRaidResult.reset();
    ++candidate.revision;

    const ProfileValidationResult validation =
        validateProfileState(candidate, content);
    if (!validation.valid)
    {
        return {false, RaidLifecycleError::InvalidProfile,
                validation.message, profile.revision};
    }
    profile = std::move(candidate);
    return {true, RaidLifecycleError::None, {}, profile.revision};
}

InventoryReceipt pickupRaidLoot(
    ProfileState &profile,
    const ContentRegistry &content,
    AssetInstanceId assetId,
    const CommandContext &context)
{
    const AssetRecord *asset = profile.assets.find(assetId);
    if (asset == nullptr ||
        !std::holds_alternative<RaidGroundAssetLocation>(asset->location))
    {
        return {false, false, DomainErrorCode::MissingAsset,
                "Raid Loot is not on the ground", profile.revision};
    }
    if (!profile.pendingRaid.has_value())
    {
        return {false, false, DomainErrorCode::InvalidProfile,
                "Raid Loot has no pending Raid", profile.revision};
    }
    const auto snapshot = std::find_if(
        profile.pendingRaid->loot.begin(),
        profile.pendingRaid->loot.end(),
        [assetId](const RaidLootSnapshot &loot)
        { return loot.assetId == assetId && !loot.collected; });
    if (snapshot == profile.pendingRaid->loot.end())
    {
        return {false, false, DomainErrorCode::InvalidProfile,
                "Raid Loot snapshot is unavailable", profile.revision};
    }

    const ItemDefinition &definition = content.item(asset->definitionId);
    ProfileState candidateProfile = profile;
    auto candidateSnapshot = std::find_if(
        candidateProfile.pendingRaid->loot.begin(),
        candidateProfile.pendingRaid->loot.end(),
        [assetId](const RaidLootSnapshot &loot)
        { return loot.assetId == assetId; });
    candidateSnapshot->collected = true;
    const auto commitMove = [&](const InventoryCommand &command)
    {
        InventoryReceipt receipt = executeInventory(
            candidateProfile,
            content,
            command,
            context);
        if (receipt.succeeded)
        {
            profile = std::move(candidateProfile);
            receipt.revision = profile.revision;
        }
        return receipt;
    };
    for (EquipmentSlotKind slot : {
             EquipmentSlotKind::PrimaryWeapon,
             EquipmentSlotKind::SecondaryWeapon,
             EquipmentSlotKind::Sidearm,
             EquipmentSlotKind::ChestRig,
             EquipmentSlotKind::Backpack,
             EquipmentSlotKind::Helmet,
             EquipmentSlotKind::BodyArmor})
    {
        if (itemCanEquipInSlot(definition, slot) &&
            !equippedAsset(candidateProfile, slot).has_value())
        {
            return commitMove(InventoryEquipCommand{assetId, slot});
        }
    }
    for (EquipmentSlotKind slot : {
             EquipmentSlotKind::Backpack,
             EquipmentSlotKind::ChestRig})
    {
        for (ProfileContainerId container :
             carriedContainers(candidateProfile, content, slot))
        {
            for (const auto &[candidateId, candidate] :
                 candidateProfile.assets.records())
            {
                static_cast<void>(candidateId);
                const auto *stored =
                    std::get_if<StoredAssetLocation>(&candidate.location);
                if (stored == nullptr || stored->container != container ||
                    candidate.definitionId != asset->definitionId ||
                    candidate.reliefBatchId != asset->reliefBatchId ||
                    definition.maxStackSize <= 1 ||
                    candidate.quantity >= definition.maxStackSize)
                {
                    continue;
                }
                return commitMove(
                    InventoryMoveCommand{
                        assetId,
                        0,
                        *stored,
                        candidate.orientation});
            }
            const auto origin = findFirstProfileFit(
                candidateProfile,
                content,
                container,
                definition,
                asset->orientation);
            if (!origin.has_value())
            {
                continue;
            }
            return commitMove(
                InventoryMoveCommand{
                    assetId,
                    0,
                    StoredAssetLocation{container, *origin},
                    asset->orientation});
        }
    }
    return {false, false, DomainErrorCode::Capacity,
            "equipped containers cannot hold this Loot", profile.revision};
}
