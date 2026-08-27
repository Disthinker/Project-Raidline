#pragma once

#include <algorithm>
#include <array>
#include <compare>
#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <variant>
#include <vector>

#include "content_registry.h"
#include "grid_inventory.h"
#include "medical_types.h"
#include "raid_intelligence_types.h"
#include "raid_map_generation.h"
#include "world_clock.h"

using AssetInstanceId = std::uint64_t;
using ProfileRevision = std::uint64_t;
using BaseServiceJobId = std::uint64_t;
using RecoveryTaskId = std::uint64_t;

enum class ProfileContainerKind
{
    Stash,
    BaseIntake,
    AssetCompartment
};

struct ProfileContainerId
{
    ProfileContainerKind kind{ProfileContainerKind::Stash};
    AssetInstanceId ownerAssetId{};
    std::uint32_t compartmentIndex{};

    [[nodiscard]] static ProfileContainerId stash() noexcept;
    [[nodiscard]] static ProfileContainerId baseIntake() noexcept;
    [[nodiscard]] static ProfileContainerId compartment(
        AssetInstanceId ownerAssetId,
        std::uint32_t compartmentIndex) noexcept;

    friend auto operator<=>(
        const ProfileContainerId &,
        const ProfileContainerId &) = default;
};

struct StoredAssetLocation
{
    ProfileContainerId container;
    GridPosition origin;

    friend bool operator==(
        const StoredAssetLocation &,
        const StoredAssetLocation &) = default;
};

struct EquippedAssetLocation
{
    EquipmentSlotKind slot{EquipmentSlotKind::PrimaryWeapon};

    friend bool operator==(
        const EquippedAssetLocation &,
        const EquippedAssetLocation &) = default;
};

struct InstalledMagazineLocation
{
    AssetInstanceId weaponAssetId{};

    friend bool operator==(
        const InstalledMagazineLocation &,
        const InstalledMagazineLocation &) = default;
};

struct RaidGroundAssetLocation
{
    std::string raidId;
    std::uint32_t lootSlotIndex{};

    friend bool operator==(
        const RaidGroundAssetLocation &,
        const RaidGroundAssetLocation &) = default;
};

struct BaseServiceAssetLocation
{
    BaseServiceJobId jobId{};

    friend bool operator==(
        const BaseServiceAssetLocation &,
        const BaseServiceAssetLocation &) = default;
};

// A failed Raid transfers only the carried ownership roots into this location.
// Installed magazines and container contents keep their existing parent
// relationship so an entire lost loadout remains one coherent asset tree.
struct LostRaidAssetLocation
{
    std::string recordId;
    EquipmentSlotKind sourceSlot{EquipmentSlotKind::PrimaryWeapon};

    friend bool operator==(
        const LostRaidAssetLocation &,
        const LostRaidAssetLocation &) = default;
};

// An NPC recovery task exclusively owns the complete lost loadout while it is
// in progress or waiting for collection. Children retain their parent
// locations; only the equipment roots move here.
struct RecoveryTaskAssetLocation
{
    RecoveryTaskId taskId{};
    EquipmentSlotKind sourceSlot{EquipmentSlotKind::PrimaryWeapon};

    friend bool operator==(
        const RecoveryTaskAssetLocation &,
        const RecoveryTaskAssetLocation &) = default;
};

using AssetLocation = std::variant<
    StoredAssetLocation,
    EquippedAssetLocation,
    InstalledMagazineLocation,
    RaidGroundAssetLocation,
    BaseServiceAssetLocation,
    LostRaidAssetLocation,
    RecoveryTaskAssetLocation>;

struct MagazineRoundRecord
{
    ItemDefinitionId definitionId;
    std::optional<std::string> reliefBatchId;

    friend bool operator==(
        const MagazineRoundRecord &,
        const MagazineRoundRecord &) = default;
};

struct AssetRecord
{
    AssetInstanceId instanceId{};
    ItemDefinitionId definitionId;
    std::uint32_t quantity{1};
    ItemOrientation orientation{ItemOrientation::Degrees0};
    std::uint32_t remainingCharges{};
    std::uint32_t currentMaximumDurability{};
    std::uint32_t currentDurability{};
    std::optional<std::string> reliefBatchId;
    std::vector<MagazineRoundRecord> magazineRounds;
    std::optional<MagazineRoundRecord> chamberedRound;
    WeaponMalfunctionType weaponMalfunction{WeaponMalfunctionType::None};
    AssetLocation location{StoredAssetLocation{
        ProfileContainerId::stash(),
        GridPosition{}}};
};

enum class WeaponReliabilityTier
{
    Reliable,
    Worn,
    HighRisk,
    Critical,
    Broken
};

[[nodiscard]] WeaponReliabilityTier weaponReliabilityTier(
    const AssetRecord &weapon,
    const ItemDefinition &definition) noexcept;

class AssetRegistry
{
public:
    [[nodiscard]] AssetInstanceId nextAssetId() const noexcept;
    void setNextAssetIdForLoad(AssetInstanceId nextAssetId);

    [[nodiscard]] AssetInstanceId create(
        const ItemDefinition &definition,
        AssetLocation location,
        std::uint32_t quantity = 1,
        std::optional<std::string> reliefBatchId = std::nullopt);

    [[nodiscard]] bool insertLoaded(AssetRecord record);
    [[nodiscard]] bool erase(AssetInstanceId instanceId) noexcept;

    [[nodiscard]] const AssetRecord *find(
        AssetInstanceId instanceId) const noexcept;
    [[nodiscard]] AssetRecord *findMutable(
        AssetInstanceId instanceId) noexcept;

    [[nodiscard]] const std::map<AssetInstanceId, AssetRecord> &
    records() const noexcept;

private:
    AssetInstanceId nextAssetId_{1};
    std::map<AssetInstanceId, AssetRecord> records_;
};

enum class TutorialProgress
{
    FindStorage,
    PrepareLoadout,
    FindRaidGate,
    Complete
};

enum class RaidResultOutcome
{
    Extracted,
    PlayerDead,
    ActiveQuit,
    AbnormalQuit
};

inline constexpr std::uint32_t kLostRaidRecordRetainedSettlementCount = 3U;

struct LostRaidRecord
{
    std::string recordId;
    std::string raidId;
    std::string settlementId;
    MapDefinitionId mapDefinitionId;
    std::string difficulty;
    RaidResultOutcome outcome{RaidResultOutcome::PlayerDead};
    std::uint64_t createdWorldMinute{};
    std::uint32_t subsequentRaidSettlementCount{};

    friend bool operator==(
        const LostRaidRecord &,
        const LostRaidRecord &) = default;
};

struct RecoveryTask
{
    RecoveryTaskId taskId{};
    LostRaidRecord sourceRecord;
    std::uint32_t paidCurrency{};
    std::uint64_t startedWorldMinute{};
    std::uint64_t completionWorldMinute{};
    bool readyForCollection{};
    std::set<AssetInstanceId> recoveredAssetIds;

    friend bool operator==(
        const RecoveryTask &,
        const RecoveryTask &) = default;
};

struct RaidEnemySnapshot
{
    Vec2 position{};
    Vec2 size{50.0F, 50.0F};
    int maximumHealth{};
    RaidSpaceDefinitionId spaceId{outdoorRaidSpaceId()};
};

struct RaidLootSnapshot
{
    AssetInstanceId assetId{};
    ItemDefinitionId definitionId;
    std::uint32_t quantity{};
    std::uint32_t slotIndex{};
    Vec2 position{};
    bool requiresHighRisk{};
    bool collected{};
    RaidSpaceDefinitionId spaceId{outdoorRaidSpaceId()};
};

// A pending self-recovery snapshot is a non-owning reference until the cache
// is opened. Asset ownership remains with LostRaidRecord before that atomic
// transition, then moves to RaidGround/one carried tree.
struct RaidSelfRecoveryRootSnapshot
{
    AssetInstanceId assetId{};
    EquipmentSlotKind sourceSlot{EquipmentSlotKind::PrimaryWeapon};
    std::uint32_t lootSlotIndex{};
    Vec2 position{};

    friend bool operator==(
        const RaidSelfRecoveryRootSnapshot &,
        const RaidSelfRecoveryRootSnapshot &) = default;
};

struct RaidSelfRecoverySnapshot
{
    LostRaidRecord sourceRecord;
    Vec2 cachePosition{};
    float interactionDurationSeconds{2.0F};
    bool opened{};
    std::vector<RaidSelfRecoveryRootSnapshot> roots;

    friend bool operator==(
        const RaidSelfRecoverySnapshot &,
        const RaidSelfRecoverySnapshot &) = default;
};

struct RaidInteriorSnapshot
{
    RaidSpaceDefinitionId id;
    std::string displayName;
    bool layoutKnown{};
    Vec2 worldSize{};
    ContentRect exteriorEntrance;
    Vec2 exteriorReturn{};
    Vec2 interiorSpawn{};
    ContentRect interiorExit;
    std::vector<ContentRect> ballisticBlockers;

    friend bool operator==(
        const RaidInteriorSnapshot &left,
        const RaidInteriorSnapshot &right)
    {
        return left.id == right.id &&
            left.displayName == right.displayName &&
            left.layoutKnown == right.layoutKnown &&
            left.worldSize.x == right.worldSize.x &&
            left.worldSize.y == right.worldSize.y &&
            left.exteriorEntrance == right.exteriorEntrance &&
            left.exteriorReturn.x == right.exteriorReturn.x &&
            left.exteriorReturn.y == right.exteriorReturn.y &&
            left.interiorSpawn.x == right.interiorSpawn.x &&
            left.interiorSpawn.y == right.interiorSpawn.y &&
            left.interiorExit == right.interiorExit &&
            left.ballisticBlockers == right.ballisticBlockers;
    }
};

struct BaseResourceState
{
    BaseResourceBundle pool{40, 40, 40, 40};
    BaseResourceBundle lastShortfall;
    std::uint64_t resolvedDemandCycleCount{};

    friend bool operator==(
        const BaseResourceState &,
        const BaseResourceState &) = default;
};

// A supply assignment authorizes the Base to consume a matching owned item
// definition for one specific need. The item stays in its real inventory
// location until a world-time demand boundary actually consumes it.
enum class BaseSupplyCategory
{
    Food,
    Medical,
    Recreation,
    Security
};

struct BaseSupplyPolicyState
{
    std::map<ItemDefinitionId, BaseSupplyCategory> assignments;

    friend bool operator==(
        const BaseSupplyPolicyState &,
        const BaseSupplyPolicyState &) = default;
};

inline constexpr std::size_t kBaseResidentProfessionCount = 4U;

using BaseProfessionCounts =
    std::array<std::uint32_t, kBaseResidentProfessionCount>;

struct RaidRescueSnapshot
{
    RescueDefinitionId definitionId;
    RaidRescueSubjectKind subjectKind{RaidRescueSubjectKind::OrdinaryResidents};
    ContentRect transferPoint;
    float interactionDurationSeconds{};
    std::uint32_t ordinaryResidentCount{};
    std::uint32_t injuredResidentCount{};
    BaseResidentProfession profession{BaseResidentProfession::General};
    bool secured{};

    friend bool operator==(
        const RaidRescueSnapshot &,
        const RaidRescueSnapshot &) = default;
};

// Ordinary residents are deliberately aggregated. The player and future
// named NPCs have separate ownership and are not counted here.
struct BasePopulationState
{
    std::uint32_t ordinaryResidents{8};
    std::uint32_t bedCapacity{10};
    std::uint32_t injuredResidents{};
    BaseProfessionCounts professionResidents{6U, 1U, 1U, 0U};
    BaseProfessionCounts injuredByProfession{};

    BasePopulationState() = default;

    BasePopulationState(
        std::uint32_t ordinary,
        std::uint32_t beds,
        std::uint32_t injured = 0U) noexcept
        : ordinaryResidents{ordinary},
          bedCapacity{beds},
          injuredResidents{injured}
    {
        const std::uint32_t specialists = std::min(ordinary, 2U);
        professionResidents = {
            ordinary - specialists,
            specialists >= 1U ? 1U : 0U,
            specialists >= 2U ? 1U : 0U,
            0U};
        injuredByProfession[static_cast<std::size_t>(
            BaseResidentProfession::General)] = injured;
    }

    friend bool operator==(
        const BasePopulationState &,
        const BasePopulationState &) = default;
};

struct BaseWorkforceState
{
    std::optional<BaseResidentProfession> workshopWorker{
        BaseResidentProfession::Engineering};
    std::optional<BaseResidentProfession> medicalWorker{
        BaseResidentProfession::Medical};

    friend bool operator==(
        const BaseWorkforceState &,
        const BaseWorkforceState &) = default;
};

enum class BaseMoraleTier
{
    Low,
    Stable,
    High
};

enum class BaseMoraleTrend
{
    Falling,
    Steady,
    Rising
};

struct BaseMoraleDailyLedger
{
    std::uint64_t dayIndex{};
    BaseResourceBundle resourceShortfall;
    std::uint32_t bedShortfall{};
    std::uint64_t fulfilledWishCount{};
    std::uint64_t missedWishCount{};
    std::uint64_t positiveEventCount{};
    std::uint64_t negativeEventCount{};
    std::int32_t netScore{};

    friend bool operator==(
        const BaseMoraleDailyLedger &,
        const BaseMoraleDailyLedger &) = default;
};

struct BaseMoraleState
{
    BaseMoraleTier tier{BaseMoraleTier::Stable};
    BaseMoraleTrend trend{BaseMoraleTrend::Steady};
    std::uint64_t resolvedDayCount{};
    std::uint64_t consecutiveLowDays{};
    std::uint32_t supportedRecoveryDays{};
    std::uint64_t pendingFulfilledWishCount{};
    std::uint64_t pendingMissedWishCount{};
    std::uint64_t pendingPositiveEventCount{};
    std::uint64_t pendingNegativeEventCount{};
    BaseMoraleDailyLedger lastLedger;

    friend bool operator==(
        const BaseMoraleState &,
        const BaseMoraleState &) = default;
};

struct BaseCommunityEventState
{
    BaseCommunityEventDefinitionId definitionId;
    std::uint64_t cycleIndex{};

    friend bool operator==(
        const BaseCommunityEventState &,
        const BaseCommunityEventState &) = default;
};

struct ActiveResidentTreatment
{
    BaseServiceJobId jobId{};
    std::uint64_t startedWorldMinute{};
    std::uint64_t completionWorldMinute{};
    std::uint32_t consumedContribution{};
    BaseResidentProfession patientProfession{BaseResidentProfession::General};
    BaseResidentProfession workerProfession{BaseResidentProfession::General};

    friend bool operator==(
        const ActiveResidentTreatment &,
        const ActiveResidentTreatment &) = default;
};

struct BaseResidentMedicalState
{
    std::optional<ActiveResidentTreatment> activeTreatment;

    friend bool operator==(
        const BaseResidentMedicalState &,
        const BaseResidentMedicalState &) = default;
};

struct BaseManufacturingOrder
{
    BaseServiceJobId jobId{};
    BaseManufacturingRecipeDefinitionId recipeDefinitionId;
    std::uint32_t committedWorkers{};
    BaseResidentProfession workerProfession{BaseResidentProfession::General};
    std::uint64_t startedWorldMinute{};
    std::uint64_t completionWorldMinute{};
    std::vector<AssetInstanceId> inputAssetIds;
    AssetInstanceId outputAssetId{};
    bool outputReady{};

    friend bool operator==(
        const BaseManufacturingOrder &,
        const BaseManufacturingOrder &) = default;
};

struct BaseManufacturingState
{
    std::optional<BaseManufacturingOrder> activeOrder;

    friend bool operator==(
        const BaseManufacturingState &,
        const BaseManufacturingState &) = default;
};

struct ActiveBaseConstructionProject
{
    BaseConstructionProjectDefinitionId definitionId;
    std::uint32_t lockedMaterialUnits{};
    std::uint32_t committedWorkers{};
    std::uint64_t startedWorldMinute{};
    std::uint64_t completionWorldMinute{};

    friend bool operator==(
        const ActiveBaseConstructionProject &,
        const ActiveBaseConstructionProject &) = default;
};

struct BaseConstructionState
{
    std::uint32_t materialUnits{};
    std::uint32_t dormitoryLevel{1};
    std::uint32_t workshopLevel{1};
    std::uint32_t medicalLevel{1};
    std::optional<ActiveBaseConstructionProject> activeProject;

    friend bool operator==(
        const BaseConstructionState &,
        const BaseConstructionState &) = default;
};

struct BasePriorityState
{
    BasePriorityDefinitionId definitionId;
    std::uint64_t cycleIndex{};
    bool fulfilled{};
    std::uint64_t missedCycleCount{};

    friend bool operator==(
        const BasePriorityState &,
        const BasePriorityState &) = default;
};

struct GunsmithMaintenanceJob
{
    BaseServiceJobId jobId{};
    AssetInstanceId weaponAssetId{};
    GridPosition returnOrigin;
    std::uint64_t startedWorldMinute{};
    std::uint64_t completionWorldMinute{};
    std::uint32_t paidCurrency{};
    std::uint32_t targetFactoryDurabilityCenti{};

    friend bool operator==(
        const GunsmithMaintenanceJob &,
        const GunsmithMaintenanceJob &) = default;
};

struct RaidTravelSnapshot
{
    std::uint32_t outboundMinutes{};
    std::uint32_t returnMinutes{};
    std::uint32_t failureRegroupMinutes{};
    WorldClockState startingWorldClock;
    BaseResourceState startingBaseResources;
    BasePriorityState startingBasePriority;
    BaseMoraleState startingBaseMorale;
    BaseCommunityEventState startingBaseCommunityEvent;
    BaseConstructionState startingBaseConstruction;
    BaseWorkforceState startingBaseWorkforce;
    std::uint32_t startingBedCapacity{10};
    std::uint32_t startingInjuredResidents{};
    BaseProfessionCounts startingInjuredByProfession{};
    BaseResidentMedicalState startingResidentMedical;
    RaidIntelligenceArchiveState startingRaidIntelligence;

    friend bool operator==(
        const RaidTravelSnapshot &,
        const RaidTravelSnapshot &) = default;
};

struct PendingRaidSnapshot
{
    std::string raidId;
    std::string settlementId;
    std::string rulesVersion;
    MapDefinitionId mapDefinitionId;
    std::uint64_t seed{};
    std::string spawnExtractionPairId;
    EnemyDeploymentDefinitionId enemyDeploymentId;
    Vec2 playerSpawn{};
    ContentRect extractionPoint;
    std::vector<RaidEnemySnapshot> enemies;
    std::vector<RaidLootSnapshot> loot;
    std::optional<RaidRescueSnapshot> rescue;
    std::optional<RaidSelfRecoverySnapshot> selfRecovery;
    RaidGeneratedMapLayout spatialLayout;
    std::vector<RaidInteriorSnapshot> interiors;
    std::vector<AssetInstanceId> carriedRootAssetIds;
    int startingHealth{100};
    MedicalStatusState startingMedicalStatus;
    RaidIntelligenceLoadout intelligence;
    RaidTravelSnapshot travel;
};

struct LastRaidResult
{
    std::string settlementId;
    RaidResultOutcome outcome{RaidResultOutcome::PlayerDead};
    std::vector<ItemDefinitionId> returnedItemDefinitionIds;
    std::int64_t currencyDelta{};
    std::uint32_t travelMinutesApplied{};
    std::uint32_t rescuedOrdinaryResidents{};
    std::uint32_t rescuedInjuredResidents{};
    std::optional<std::string> lostRaidRecordId;
};

struct ProfileState
{
    std::string profileId;
    ProfileRevision revision{1};
    std::uint32_t currency{};
    TutorialProgress tutorial{TutorialProgress::FindStorage};
    int currentHealth{100};
    MedicalStatusState medicalStatus;
    WorldClockState worldClock;
    BaseResourceState baseResources;
    BaseSupplyPolicyState baseSupplyPolicy;
    BasePopulationState basePopulation;
    BaseWorkforceState baseWorkforce;
    BaseMoraleState baseMorale;
    BaseCommunityEventState baseCommunityEvent;
    BaseResidentMedicalState residentMedical;
    BaseManufacturingState baseManufacturing;
    BaseConstructionState baseConstruction;
    BasePriorityState basePriority;
    RaidIntelligenceArchiveState raidIntelligence;
    RaidInteriorIntelligenceArchiveState raidInteriorIntelligence;
    BaseServiceJobId nextBaseServiceJobId{1};
    std::optional<GunsmithMaintenanceJob> gunsmithMaintenanceJob;
    AssetRegistry assets;
    std::map<std::string, LostRaidRecord> lostRaidRecords;
    RecoveryTaskId nextRecoveryTaskId{1};
    std::optional<RecoveryTask> recoveryTask;
    std::set<std::string> committedTransactions;
    std::set<std::string> committedSettlements;
    std::set<RescueDefinitionId> committedRescues;
    std::optional<PendingRaidSnapshot> pendingRaid;
    std::optional<LastRaidResult> lastRaidResult;
};

struct ProfileValidationResult
{
    bool valid{};
    std::string message;
};

[[nodiscard]] ProfileState makeNewAlphaProfile(
    std::string profileId,
    const ContentRegistry &content);

[[nodiscard]] ProfileValidationResult validateProfileState(
    const ProfileState &profile,
    const ContentRegistry &content);

[[nodiscard]] std::uint64_t profileStateFingerprint(
    const ProfileState &profile) noexcept;

[[nodiscard]] InventoryGridSize profileContainerSize(
    const ProfileState &profile,
    const ContentRegistry &content,
    ProfileContainerId container);

[[nodiscard]] std::vector<const AssetRecord *> assetsInContainer(
    const ProfileState &profile,
    ProfileContainerId container);

[[nodiscard]] std::optional<AssetInstanceId> equippedAsset(
    const ProfileState &profile,
    EquipmentSlotKind slot) noexcept;

[[nodiscard]] std::optional<AssetInstanceId> installedMagazine(
    const ProfileState &profile,
    AssetInstanceId weaponAssetId) noexcept;

[[nodiscard]] bool assetIsCarried(
    const ProfileState &profile,
    AssetInstanceId instanceId) noexcept;

// Base allocation commands may explicitly consume an asset from the personal
// Stash or the equipped ownership tree without moving it into a transit
// container first. Service-held and Raid-ground assets are not accessible.
[[nodiscard]] bool assetIsBaseAccessible(
    const ProfileState &profile,
    AssetInstanceId instanceId) noexcept;

[[nodiscard]] std::vector<AssetInstanceId> carriedAssetIds(
    const ProfileState &profile);

// Gross carried mass includes equipped roots, nested carried contents,
// installed magazines, magazine rounds, and chambered rounds exactly once.
[[nodiscard]] std::uint64_t carriedWeightGrams(
    const ProfileState &profile,
    const ContentRegistry &content) noexcept;

[[nodiscard]] std::optional<AssetInstanceId> profileAssetAtCell(
    const ProfileState &profile,
    const ContentRegistry &content,
    ProfileContainerId container,
    GridPosition cell) noexcept;

[[nodiscard]] std::optional<GridPosition> findFirstProfileFit(
    const ProfileState &profile,
    const ContentRegistry &content,
    ProfileContainerId container,
    const ItemDefinition &definition,
    ItemOrientation orientation,
    std::optional<AssetInstanceId> ignoredAsset = std::nullopt) noexcept;

[[nodiscard]] bool profilePlacementFits(
    const ProfileState &profile,
    const ContentRegistry &content,
    ProfileContainerId container,
    GridPosition origin,
    const ItemDefinition &definition,
    ItemOrientation orientation,
    std::optional<AssetInstanceId> ignoredAsset = std::nullopt) noexcept;
