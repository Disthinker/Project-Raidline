#include "raid_lifecycle.h"

#include <algorithm>
#include <limits>
#include <set>

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
        map->raidLootSlots.size() < 8)
    {
        return deployFailure(
            RaidLifecycleError::InvalidCommand,
            "Deploy map has no validated Alpha configuration",
            profile.revision);
    }

    Pcg32 configurationRandom{command.seed, 0x6d61702d636f6e66ULL};
    Pcg32 lootRandom{command.seed, 0x6c6f6f742d726169ULL};
    const std::size_t pairIndex = configurationRandom.bounded(3U);
    const std::size_t deploymentIndex = configurationRandom.bounded(3U);
    const auto &pair = map->spawnExtractionPairs[pairIndex];
    const auto &deployment = content.enemyDeployment(
        map->raidEnemyDeploymentIds[deploymentIndex]);

    PendingRaidSnapshot snapshot;
    snapshot.raidId = command.raidId;
    snapshot.settlementId = command.settlementId;
    snapshot.rulesVersion = "core-alpha-raid-1";
    snapshot.mapDefinitionId = command.mapDefinitionId;
    snapshot.seed = command.seed;
    snapshot.spawnExtractionPairId = pair.id;
    snapshot.enemyDeploymentId = deployment.id;
    snapshot.playerSpawn = pair.playerSpawn;
    snapshot.extractionPoint = pair.extractionPoint;
    snapshot.startingHealth = candidate.currentHealth;
    for (const EnemySpawnDefinition &enemy : deployment.enemies)
    {
        snapshot.enemies.push_back(
            RaidEnemySnapshot{enemy.position, enemy.size, enemy.maximumHealth});
    }
    for (EquipmentSlotKind slot : {
             EquipmentSlotKind::PrimaryWeapon,
             EquipmentSlotKind::ChestRig,
             EquipmentSlotKind::Backpack})
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
            static_cast<std::uint32_t>(slotIndex),
            map->raidLootSlots[slotIndex].position});
    }

    candidate.pendingRaid = std::move(snapshot);
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
    }
    candidate.pendingRaid.reset();
    candidate.committedSettlements.insert(id);
    candidate.lastRaidResult = LastRaidResult{
        id,
        outcome,
        std::move(returned),
        0};
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
    const ItemDefinition &definition = content.item(asset->definitionId);
    for (EquipmentSlotKind slot : {
             EquipmentSlotKind::Backpack,
             EquipmentSlotKind::ChestRig})
    {
        for (ProfileContainerId container :
             carriedContainers(profile, content, slot))
        {
            for (const auto &[candidateId, candidate] :
                 profile.assets.records())
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
                return executeInventory(
                    profile,
                    content,
                    InventoryMoveCommand{
                        assetId,
                        0,
                        *stored,
                        candidate.orientation},
                    context);
            }
            const auto origin = findFirstProfileFit(
                profile,
                content,
                container,
                definition,
                asset->orientation);
            if (!origin.has_value())
            {
                continue;
            }
            return executeInventory(
                profile,
                content,
                InventoryMoveCommand{
                    assetId,
                    0,
                    StoredAssetLocation{container, *origin},
                    asset->orientation},
                context);
        }
    }
    return {false, false, DomainErrorCode::Capacity,
            "equipped containers cannot hold this Loot", profile.revision};
}
