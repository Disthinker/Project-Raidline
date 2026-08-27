#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "definition_id.h"
#include "item_definition.h"
#include "raid_intelligence_types.h"
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

    friend bool operator==(ContentRect first, ContentRect second) noexcept
    {
        return first.position.x == second.position.x &&
            first.position.y == second.position.y &&
            first.size.x == second.size.x &&
            first.size.y == second.size.y;
    }
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
    ContentRect conditionalExtractionPoint;
    float conditionalExtractionDurationSeconds{};
    std::uint64_t conditionalExtractionMaximumWeightGrams{};
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

struct RaidTravelDefinition
{
    std::uint32_t outboundMinutes{};
    std::uint32_t returnMinutes{};
    std::uint32_t failureRegroupMinutes{};

    friend bool operator==(
        const RaidTravelDefinition &,
        const RaidTravelDefinition &) = default;
};

struct GunsmithFullMaintenanceDefinition
{
    std::uint32_t baseCost{};
    std::uint32_t currentDurabilityCostPerPoint{};
    std::uint32_t maximumDurabilityCostPerPoint{};

    friend bool operator==(
        const GunsmithFullMaintenanceDefinition &,
        const GunsmithFullMaintenanceDefinition &) = default;
};

[[nodiscard]] const RaidSpaceDefinitionId &outdoorRaidSpaceId();

struct RaidExteriorPlacementDefinition
{
    std::string id;
    ContentRect entrance;
    Vec2 returnPoint{};

    friend bool operator==(
        const RaidExteriorPlacementDefinition &left,
        const RaidExteriorPlacementDefinition &right) noexcept
    {
        return left.id == right.id &&
            left.entrance == right.entrance &&
            left.returnPoint.x == right.returnPoint.x &&
            left.returnPoint.y == right.returnPoint.y;
    }
};

struct RaidInteriorDefinition
{
    RaidSpaceDefinitionId id;
    std::string displayName;
    std::uint32_t intelligencePrice{};
    Vec2 worldSize{};
    // The first candidate remains the legacy fixed portal for rules v12.
    ContentRect exteriorEntrance;
    Vec2 exteriorReturn{};
    std::vector<RaidExteriorPlacementDefinition> exteriorPlacements;
    Vec2 interiorSpawn{};
    ContentRect interiorExit;
    std::vector<BallisticBlockerDefinition> ballisticBlockers;
    std::vector<EnemySpawnDefinition> enemies;
    LootTableDefinitionId lootTableId;
    std::vector<RaidLootSlotDefinition> lootSlots;

    friend bool operator==(
        const RaidInteriorDefinition &,
        const RaidInteriorDefinition &) = default;
};

struct RaidOperationBriefingDefinition
{
    std::string difficulty;
    std::string warning;
    std::array<std::uint32_t, kRaidIntelligenceCategoryCount> prices{};

    [[nodiscard]] std::uint32_t price(
        RaidIntelligenceCategory category) const noexcept
    {
        return category == RaidIntelligenceCategory::Count
            ? 0U
            : prices[raidIntelligenceCategoryIndex(category)];
    }

    friend bool operator==(
        const RaidOperationBriefingDefinition &,
        const RaidOperationBriefingDefinition &) = default;
};

enum class RegionNodeKind
{
    Base,
    Raid,
    Outpost
};

struct RegionNodeDefinition
{
    RegionNodeDefinitionId id;
    std::string displayName;
    RegionNodeKind kind{RegionNodeKind::Raid};
    std::optional<MapDefinitionId> mapDefinitionId;

    friend bool operator==(
        const RegionNodeDefinition &,
        const RegionNodeDefinition &) = default;
};

struct RegionalOutpostDefinition
{
    RegionalOutpostDefinitionId id;
    std::string displayName;
    RegionNodeDefinitionId nodeId;
    bool initiallyUnlocked{};
    std::uint32_t requiredStaff{};
    std::uint32_t safeShortcutOperations{};
    MapDefinitionId restorationMapDefinitionId;

    friend bool operator==(
        const RegionalOutpostDefinition &,
        const RegionalOutpostDefinition &) = default;
};

enum class RegionalBaseSiteTier
{
    Basic,
    Mature,
    Strategic
};

struct RegionalBaseSiteDefinition
{
    RegionalBaseSiteDefinitionId id;
    std::string displayName;
    RegionNodeDefinitionId nodeId;
    RegionalBaseSiteTier tier{RegionalBaseSiteTier::Basic};
    bool initiallyUnlocked{};
    std::optional<MapDefinitionId> clearanceMapDefinitionId;
    std::optional<RegionalOutpostDefinitionId> outpostDefinitionId;
    std::string advantage;
    std::string disadvantage;
    std::string uniqueFeature;
    std::uint32_t migrationMinutes{};
    std::uint32_t coreFacilitySlots{};

    friend bool operator==(
        const RegionalBaseSiteDefinition &,
        const RegionalBaseSiteDefinition &) = default;
};

struct RegionRouteDefinition
{
    RegionRouteDefinitionId id;
    std::string displayName;
    RegionNodeDefinitionId from;
    RegionNodeDefinitionId to;
    std::uint32_t travelMinutes{};
    std::optional<RegionalOutpostDefinitionId> requiredOnlineOutpostId;

    friend bool operator==(
        const RegionRouteDefinition &,
        const RegionRouteDefinition &) = default;
};

struct RegionalOperationsDefinition
{
    RegionNodeDefinitionId initialBaseNodeId;
    std::uint32_t maximumEstablishedOutposts{};
    std::vector<RegionNodeDefinition> nodes;
    std::vector<RegionalBaseSiteDefinition> baseSites;
    std::vector<RegionalOutpostDefinition> outposts;
    std::vector<RegionRouteDefinition> routes;
};

struct RaidRecoveryDefinition
{
    std::uint32_t serviceFee{};
    std::uint32_t durationMinutes{};

    friend bool operator==(
        const RaidRecoveryDefinition &,
        const RaidRecoveryDefinition &) = default;
};

enum class RaidRescueSubjectKind
{
    OrdinaryResidents
};

enum class BaseResidentProfession
{
    General,
    Medical,
    Engineering,
    Combat
};

struct RaidRescueDefinition
{
    RescueDefinitionId id;
    RaidRescueSubjectKind subjectKind{RaidRescueSubjectKind::OrdinaryResidents};
    ContentRect transferPoint;
    float interactionDurationSeconds{};
    std::uint32_t ordinaryResidentCount{};
    std::uint32_t injuredResidentCount{};
    BaseResidentProfession profession{BaseResidentProfession::General};
};

struct PlayerBaseMedicalDefinition
{
    std::uint32_t missingHealthCostPerPoint{};
    std::uint32_t lightBleedingCost{};
    std::uint32_t heavyBleedingCost{};

    friend bool operator==(
        const PlayerBaseMedicalDefinition &,
        const PlayerBaseMedicalDefinition &) = default;
};

struct ResidentMedicalDefinition
{
    std::uint32_t requiredContribution{};
    std::uint32_t durationMinutes{};

    friend bool operator==(
        const ResidentMedicalDefinition &,
        const ResidentMedicalDefinition &) = default;
};

struct BaseOperationsDefinition
{
    std::uint32_t strainedBelowReserveDays{};
    std::uint32_t supportedAtReserveDays{};

    friend bool operator==(
        const BaseOperationsDefinition &,
        const BaseOperationsDefinition &) = default;
};

struct BaseMoraleDefinition
{
    std::uint32_t recoveryDaysFromLow{};
    std::uint32_t lowManufacturingDurationPercent{};
    std::uint32_t stableManufacturingDurationPercent{};
    std::uint32_t highManufacturingDurationPercent{};
    std::uint32_t eventCycleDays{};

    friend bool operator==(
        const BaseMoraleDefinition &,
        const BaseMoraleDefinition &) = default;
};

struct BaseWorkforceDefinition
{
    std::uint32_t generalFallbackDurationPercent{};
    std::uint32_t workshopLevel2DurationPercent{};
    std::uint32_t medicalLevel2DurationPercent{};

    friend bool operator==(
        const BaseWorkforceDefinition &,
        const BaseWorkforceDefinition &) = default;
};

struct BaseCommunityEventDefinition
{
    BaseCommunityEventDefinitionId id;
    std::string displayName;
    std::string description;
    std::int32_t moraleEffect{};

    friend bool operator==(
        const BaseCommunityEventDefinition &,
        const BaseCommunityEventDefinition &) = default;
};

struct BasePriorityDefinition
{
    BasePriorityDefinitionId id;
    std::string displayName;
    ItemDefinitionId requiredItemDefinitionId;
    std::uint32_t requiredQuantity{1};
    BaseResourceBundle resourceReward;

    friend bool operator==(
        const BasePriorityDefinition &,
        const BasePriorityDefinition &) = default;
};

enum class BaseFacilityUpgradeTarget
{
    Dormitory,
    KitchenWater,
    Workshop,
    Medical
};

struct BaseFacilityDefinition
{
    BaseFacilityDefinitionId id;
    std::string displayName;
    bool requiredForMigration{};
    bool initiallyOwned{};
    bool initiallyInstalled{};

    friend bool operator==(
        const BaseFacilityDefinition &,
        const BaseFacilityDefinition &) = default;
};

struct BaseConstructionProjectDefinition
{
    BaseConstructionProjectDefinitionId id;
    std::string displayName;
    BaseFacilityUpgradeTarget target{BaseFacilityUpgradeTarget::Dormitory};
    std::uint32_t requiredLevel{};
    std::uint32_t targetLevel{};
    std::uint32_t materialCost{};
    std::uint32_t workerCount{};
    std::uint32_t durationMinutes{};
    std::uint32_t bedCapacityAfter{};

    friend bool operator==(
        const BaseConstructionProjectDefinition &,
        const BaseConstructionProjectDefinition &) = default;
};

struct BaseManufacturingInputDefinition
{
    ItemDefinitionId itemDefinitionId;
    std::uint32_t quantity{1};

    friend bool operator==(
        const BaseManufacturingInputDefinition &,
        const BaseManufacturingInputDefinition &) = default;
};

struct BaseManufacturingRecipeDefinition
{
    BaseManufacturingRecipeDefinitionId id;
    std::string displayName;
    std::vector<BaseManufacturingInputDefinition> inputs;
    ItemDefinitionId outputItemDefinitionId;
    std::uint32_t outputQuantity{1};
    std::uint32_t workerCount{1};
    std::uint32_t durationMinutes{};

    friend bool operator==(
        const BaseManufacturingRecipeDefinition &,
        const BaseManufacturingRecipeDefinition &) = default;
};

// A deliberately narrow first consumer for the future procedural Raid world.
// Content still owns every gameplay anchor; generation only varies outdoor
// cover between those validated anchors and freezes the accepted result.
struct ProceduralOutdoorDefinition
{
    bool enabled{};
    std::uint32_t columns{16};
    std::uint32_t rows{9};
    std::uint32_t minimumBlockers{18};
    std::uint32_t maximumBlockers{26};
    std::uint32_t maximumAttempts{8};
    std::uint32_t anchorClearanceCells{1};

    friend bool operator==(
        const ProceduralOutdoorDefinition &,
        const ProceduralOutdoorDefinition &) = default;
};

struct MapDefinition
{
    MapDefinitionId id;
    std::string displayName;
    std::string routeProfile;
    RaidTravelDefinition travel;
    RaidRecoveryDefinition recovery;
    RaidOperationBriefingDefinition operationBriefing;
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
    std::optional<RaidRescueDefinition> rescue;
    HighRiskRaidDefinition highRisk;
    LootTableDefinitionId storageLootTableId;
    EnemyDeploymentDefinitionId enemyDeploymentId;
    std::vector<SpawnExtractionPairDefinition> spawnExtractionPairs;
    std::vector<EnemyDeploymentDefinitionId> raidEnemyDeploymentIds;
    std::vector<RaidLootSlotDefinition> raidLootSlots;
    LootTableDefinitionId raidLootTableId;
    ProceduralOutdoorDefinition proceduralOutdoor;
    std::vector<RaidInteriorDefinition> interiors;
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
    const GunsmithFullMaintenanceDefinition &
    gunsmithFullMaintenance() const noexcept;

    [[nodiscard]] const PlayerBaseMedicalDefinition &
    playerBaseMedical() const noexcept;

    [[nodiscard]] const ResidentMedicalDefinition &
    residentMedical() const noexcept;

    [[nodiscard]]
    const BaseOperationsDefinition &baseOperations() const noexcept;

    [[nodiscard]] const BaseMoraleDefinition &baseMorale() const noexcept;

    [[nodiscard]] const BaseWorkforceDefinition &baseWorkforce() const noexcept;

    [[nodiscard]] const RegionalOperationsDefinition &
    regionalOperations() const noexcept;

    [[nodiscard]] const RegionNodeDefinition &regionNode(
        const RegionNodeDefinitionId &id) const;

    [[nodiscard]] const RegionalOutpostDefinition &regionalOutpost(
        const RegionalOutpostDefinitionId &id) const;

    [[nodiscard]] const RegionalBaseSiteDefinition &regionalBaseSite(
        const RegionalBaseSiteDefinitionId &id) const;

    [[nodiscard]] const RegionRouteDefinition &regionRoute(
        const RegionRouteDefinitionId &id) const;

    [[nodiscard]] const std::vector<BaseCommunityEventDefinition> &
    baseCommunityEvents() const noexcept;

    [[nodiscard]] const BaseCommunityEventDefinition &baseCommunityEvent(
        const BaseCommunityEventDefinitionId &id) const;

    [[nodiscard]] std::uint32_t
    basePriorityCycleMinutes() const noexcept;

    [[nodiscard]] const std::vector<BasePriorityDefinition> &
    basePriorities() const noexcept;

    [[nodiscard]] const BasePriorityDefinition &basePriority(
        const BasePriorityDefinitionId &id) const;

    [[nodiscard]] std::uint32_t
    maximumBaseConstructionMaterials() const noexcept;

    [[nodiscard]] const std::vector<BaseFacilityDefinition> &
    baseFacilities() const noexcept;

    [[nodiscard]] const BaseFacilityDefinition &baseFacility(
        const BaseFacilityDefinitionId &id) const;

    [[nodiscard]] const std::vector<BaseConstructionProjectDefinition> &
    baseConstructionProjects() const noexcept;

    [[nodiscard]] const BaseConstructionProjectDefinition &
    baseConstructionProject(
        const BaseConstructionProjectDefinitionId &id) const;

    [[nodiscard]] const std::vector<BaseManufacturingRecipeDefinition> &
    baseManufacturingRecipes() const noexcept;

    [[nodiscard]] const BaseManufacturingRecipeDefinition &
    baseManufacturingRecipe(
        const BaseManufacturingRecipeDefinitionId &id) const;

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

    [[nodiscard]] const RaidInteriorDefinition &raidInterior(
        const RaidSpaceDefinitionId &id) const;

private:
    std::string contentVersion_;
    std::vector<std::string> publishedResources_;
    std::vector<ItemDefinition> items_;
    std::vector<LootTableDefinition> lootTables_;
    std::vector<EnemyDeploymentDefinition> enemyDeployments_;
    std::vector<MapDefinition> maps_;
    GunsmithFullMaintenanceDefinition gunsmithFullMaintenance_;
    PlayerBaseMedicalDefinition playerBaseMedical_;
    ResidentMedicalDefinition residentMedical_;
    BaseOperationsDefinition baseOperations_;
    BaseMoraleDefinition baseMorale_;
    BaseWorkforceDefinition baseWorkforce_;
    RegionalOperationsDefinition regionalOperations_;
    std::vector<BaseCommunityEventDefinition> baseCommunityEvents_;
    std::uint32_t basePriorityCycleMinutes_{};
    std::vector<BasePriorityDefinition> basePriorities_;
    std::uint32_t maximumBaseConstructionMaterials_{};
    std::vector<BaseFacilityDefinition> baseFacilities_;
    std::vector<BaseConstructionProjectDefinition>
        baseConstructionProjects_;
    std::vector<BaseManufacturingRecipeDefinition>
        baseManufacturingRecipes_;

    std::map<ItemDefinitionId, std::size_t> itemIndex_;
    std::map<LootTableDefinitionId, std::size_t> lootTableIndex_;
    std::map<EnemyDeploymentDefinitionId, std::size_t>
        enemyDeploymentIndex_;
    std::map<MapDefinitionId, std::size_t> mapIndex_;
    std::map<BasePriorityDefinitionId, std::size_t> basePriorityIndex_;
    std::map<BaseCommunityEventDefinitionId, std::size_t>
        baseCommunityEventIndex_;
    std::map<BaseConstructionProjectDefinitionId, std::size_t>
        baseConstructionProjectIndex_;
    std::map<BaseFacilityDefinitionId, std::size_t> baseFacilityIndex_;
    std::map<BaseManufacturingRecipeDefinitionId, std::size_t>
        baseManufacturingRecipeIndex_;
    std::map<RegionNodeDefinitionId, std::size_t> regionNodeIndex_;
    std::map<RegionalOutpostDefinitionId, std::size_t>
        regionalOutpostIndex_;
    std::map<RegionalBaseSiteDefinitionId, std::size_t>
        regionalBaseSiteIndex_;
    std::map<RegionRouteDefinitionId, std::size_t> regionRouteIndex_;
};

[[nodiscard]]
std::string_view publishedContentJson() noexcept;

[[nodiscard]]
const ContentRegistry &publishedContentRegistry();

[[nodiscard]]
const MapDefinition &defaultV0MapDefinition();
