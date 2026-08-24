#pragma once

#include <cstdint>
#include <map>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "definition_id.h"
#include "item_definition.h"
#include "vec2.h"

class ContentRegistryError : public std::runtime_error
{
public:
    using std::runtime_error::runtime_error;
};

struct LootContentEntry
{
    ItemDefinitionId itemDefinitionId;
    std::uint32_t weight{};
    std::uint32_t minimumQuantity{1};
    std::uint32_t maximumQuantity{1};
};

struct LootTableDefinition
{
    LootTableDefinitionId id;
    std::uint32_t rollCount{};
    std::vector<LootContentEntry> entries;
};

struct EnemySpawnDefinition
{
    Vec2 position{};
    Vec2 size{};
    int maximumHealth{};
};

struct EnemyDeploymentDefinition
{
    EnemyDeploymentDefinitionId id;
    std::vector<EnemySpawnDefinition> enemies;
};

struct ContentGridSize
{
    int width{};
    int height{};
};

struct ContentColor
{
    std::uint8_t red{255};
    std::uint8_t green{255};
    std::uint8_t blue{255};
};

struct ContentRect
{
    Vec2 position{};
    Vec2 size{};
};

struct GroundItemDefinition
{
    ItemDefinitionId itemDefinitionId;
    Vec2 position{};
    std::uint32_t quantity{1};
};

struct StorageCabinetDefinition
{
    ContentRect bounds;
    float interactionRange{};
    ContentGridSize inventorySize;
};

struct RaidRuleDefinition
{
    float durationSeconds{};
    float extractionDurationSeconds{};
};

struct RaidLootSlotDefinition
{
    std::string id;
    std::string route;
    Vec2 position{};
};

struct HighRiskRaidDefinition
{
    bool enabled{};
    float regularPhaseDurationSeconds{};
    ContentRect emergencyExtractionPoint;
    float emergencyExtractionDurationSeconds{};
    float initialWaveDelaySeconds{};
    float waveIntervalSeconds{};
    std::uint32_t waveSize{};
    std::uint32_t activeEnemyCap{};
    std::vector<EnemySpawnDefinition> pressureSpawns;
    ContentRect activationControlPoint;
    float activationDurationSeconds{};
    ContentRect advancedResourceArea;
    LootTableDefinitionId advancedLootTableId;
    std::vector<RaidLootSlotDefinition> advancedLootSlots;
};

struct SpawnExtractionPairDefinition
{
    std::string id;
    Vec2 playerSpawn{};
    ContentRect extractionPoint;
};

struct BallisticBlockerDefinition
{
    std::string id;
    ContentRect bounds;
};

struct MapDefinition
{
    MapDefinitionId id;
    std::string displayName;
    std::string routeProfile;
    std::string backgroundTexturePath;
    ContentColor backgroundTint;
    Vec2 worldSize{};
    ContentRect walkableBounds;
    Vec2 playerSpawn{};
    ContentGridSize defaultInventorySize;
    std::vector<BallisticBlockerDefinition> ballisticBlockers;
    std::vector<GroundItemDefinition> groundItems;
    StorageCabinetDefinition storageCabinet;
    ContentRect extractionPoint;
    RaidRuleDefinition raidRules;
    HighRiskRaidDefinition highRisk;
    LootTableDefinitionId storageLootTableId;
    EnemyDeploymentDefinitionId enemyDeploymentId;
    std::vector<SpawnExtractionPairDefinition> spawnExtractionPairs;
    std::vector<EnemyDeploymentDefinitionId> raidEnemyDeploymentIds;
    std::vector<RaidLootSlotDefinition> raidLootSlots;
    LootTableDefinitionId raidLootTableId;
};

class ContentRegistry
{
public:
    [[nodiscard]]
    static ContentRegistry fromJson(
        std::string_view jsonText);

    [[nodiscard]]
    const std::string &contentVersion() const noexcept;

    [[nodiscard]]
    const std::vector<std::string> &
    publishedResources() const noexcept;

    [[nodiscard]]
    const std::vector<ItemDefinition> &items() const noexcept;

    [[nodiscard]]
    const std::vector<LootTableDefinition> &
    lootTables() const noexcept;

    [[nodiscard]]
    const std::vector<EnemyDeploymentDefinition> &
    enemyDeployments() const noexcept;

    [[nodiscard]]
    const std::vector<MapDefinition> &maps() const noexcept;

    [[nodiscard]]
    const ItemDefinition &item(
        const ItemDefinitionId &id) const;

    [[nodiscard]]
    const LootTableDefinition &lootTable(
        const LootTableDefinitionId &id) const;

    [[nodiscard]]
    const EnemyDeploymentDefinition &enemyDeployment(
        const EnemyDeploymentDefinitionId &id) const;

    [[nodiscard]]
    const MapDefinition &map(
        const MapDefinitionId &id) const;

private:
    std::string contentVersion_;
    std::vector<std::string> publishedResources_;
    std::vector<ItemDefinition> items_;
    std::vector<LootTableDefinition> lootTables_;
    std::vector<EnemyDeploymentDefinition> enemyDeployments_;
    std::vector<MapDefinition> maps_;

    std::map<ItemDefinitionId, std::size_t> itemIndex_;
    std::map<LootTableDefinitionId, std::size_t> lootTableIndex_;
    std::map<EnemyDeploymentDefinitionId, std::size_t>
        enemyDeploymentIndex_;
    std::map<MapDefinitionId, std::size_t> mapIndex_;
};

[[nodiscard]]
std::string_view publishedContentJson() noexcept;

[[nodiscard]]
const ContentRegistry &publishedContentRegistry();

[[nodiscard]]
const MapDefinition &defaultV0MapDefinition();
