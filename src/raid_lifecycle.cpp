#include "raid_lifecycle.h"

#include "base_construction_domain.h"
#include "base_manufacturing_domain.h"
#include "base_morale_domain.h"

#include <algorithm>
#include <limits>
#include <set>

#include "base_resource_domain.h"
#include "base_population_domain.h"
#include "base_resident_medical_domain.h"
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
}

RaidTravelPreview queryRaidTravel(
    const ProfileState &profile,
    const MapDefinition &map) noexcept
{
    WorldClockState arrivalClock = profile.worldClock;
    static_cast<void>(advanceWorldClock(
        arrivalClock,
        map.travel.outboundMinutes));
    WorldClockState extractedClock = arrivalClock;
    static_cast<void>(advanceWorldClock(
        extractedClock,
        map.travel.returnMinutes));
    WorldClockState failureClock = arrivalClock;
    static_cast<void>(advanceWorldClock(
        failureClock,
        map.travel.failureRegroupMinutes));
    return RaidTravelPreview{
        map.travel.outboundMinutes,
        map.travel.returnMinutes,
        map.travel.failureRegroupMinutes,
        projectWorldClock(profile.worldClock),
        projectWorldClock(arrivalClock),
        projectWorldClock(extractedClock),
        projectWorldClock(failureClock)};
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
    const std::size_t pairIndex = configurationRandom.bounded(3U);
    const std::size_t deploymentIndex = configurationRandom.bounded(3U);
    const auto &pair = map->spawnExtractionPairs[pairIndex];
    const auto &deployment = content.enemyDeployment(
        map->raidEnemyDeploymentIds[deploymentIndex]);

    PendingRaidSnapshot snapshot;
    snapshot.raidId = command.raidId;
    snapshot.settlementId = command.settlementId;
    snapshot.rulesVersion = "regional-map-intelligence-10";
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
        map->travel.outboundMinutes,
        map->travel.returnMinutes,
        map->travel.failureRegroupMinutes,
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

    std::vector<AssetInstanceId> eraseIds;
    for (const auto &[assetId, asset] : candidate.assets.records())
    {
        const bool onGround =
            std::holds_alternative<RaidGroundAssetLocation>(asset.location);
        const bool carried = assetIsCarried(candidate, assetId);
        if (onGround ||
            (outcome != RaidResultOutcome::Extracted && carried))
        {
            eraseIds.push_back(assetId);
        }
    }
    for (AssetInstanceId assetId : eraseIds)
    {
        static_cast<void>(candidate.assets.erase(assetId));
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
            : 0U};
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
    std::set<AssetInstanceId> generatedLoot;
    for (const RaidLootSnapshot &loot : candidate.pendingRaid->loot)
    {
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
    const auto commitMove = [&](const InventoryMoveCommand &command)
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
