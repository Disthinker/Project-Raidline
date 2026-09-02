#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "economy_domain.h"
#include "base_construction_domain.h"
#include "base_ground_domain.h"
#include "base_facility_layout_domain.h"
#include "base_migration_domain.h"
#include "base_site_feature_domain.h"
#include "base_siege_domain.h"
#include "base_workforce_domain.h"
#include "base_manufacturing_domain.h"
#include "base_resident_medical_domain.h"
#include "base_population_domain.h"
#include "base_resource_domain.h"
#include "base_medical_service_domain.h"
#include "base_service_domain.h"
#include "gameplay_world.h"
#include "inventory_domain.h"
#include "home_perimeter_domain.h"
#include "lost_raid_domain.h"
#include "recovery_task_domain.h"
#include "regional_operations_domain.h"
#include "profile_combat_domain.h"
#include "raid_action.h"
#include "raid_lifecycle.h"
#include "raid_intelligence_domain.h"
#include "raid_rescue_domain.h"
#include "raid_settlement.h"
#include "save_repository.h"
#include "self_recovery_domain.h"
#include "stash.h"
#include "maintenance_domain.h"
#include "weapon_clear_gesture.h"

enum class BaseFacilityKind;
class BaseWorld;

enum class GameSessionState
{
    InRaid,
    SettlementBlocked,
    BetweenRaids,
};

enum class DeveloperWeaponParameter
{
    RecoilControl,
    Stability,
    HandlingSpeed,
    Ergonomics,
    Accuracy,
    ShotInterval,
    BaseDamage,
    EffectiveRange,
    MaximumRange,
    LogicalBallisticSpeed,
    MaximumReticleSpeed,
    ReticleControlAcceleration,
    SpreadPerShot,
    RecoilLateralRatio,
    RecoilBendDuration,
    MovingSpreadFraction,
    SprintingSpreadFraction,
    ReticleMotionSpreadRate,
    NearDistanceSpreadScale,
    DistanceBloomAtEffectiveRange,
    AdsAccuracyMultiplier,
    AdsStabilityMultiplier,
    WeakTracerLength,
    WeakTracerOpacity,
    WeakTracerLifetime,
    Count,
};

// Client-facing semantic facts. These do not name audio files and are not
// persisted; the SDL client decides how to present them.
enum class GameSessionPresentationEvent
{
    WeaponDryFire,
    WeaponChambered,
    ReloadStarted,
    ReloadCompleted,
    MagazineLoaded,
    MagazineUnloaded,
    MedicalStarted,
    MedicalCompleted,
    MedicalInterrupted,
    WeaponEquipped,
    MalfunctionCleared,
    LootPickedUp,
    RescueSecured,
    BaseDormitoryUpgradeCompleted,
    BaseKitchenWaterUpgradeCompleted,
    BaseWorkshopUpgradeCompleted,
    BaseMedicalUpgradeCompleted,
    BaseManufacturingCompleted,
    BaseResidentTreatmentCompleted,
};

struct DeveloperWeaponTuningSnapshot
{
    AssetInstanceId weaponAssetId{};
    ItemDefinitionId weaponDefinitionId;
    WeaponUseDefinition weaponUse;
    WeaponHandlingParameters handling;
    bool overridden{};
};

struct RaidSelfRecoveryProjection
{
    std::string recordId;
    MapDefinitionId mapDefinitionId;
    Vec2 cachePosition{};
    std::size_t rootCount{};
    bool opened{};
    bool interactionInRange{};
    float interactionProgress{};
};

struct RaidOperationProjection
{
    bool basePerimeterSweepActive{};
    bool objectiveSecured{};
};

struct RaidHighRiskCrisisProjection
{
    std::string definitionId;
    std::string displayName;
    std::string warning;
    std::uint16_t districtInstanceId{};
    std::string resourcePointInstanceId;
    ContentRect focusArea;
    float initialWaveDelaySeconds{};
    float waveIntervalSeconds{};
    std::uint32_t waveSize{};
    std::uint32_t activeEnemyCap{};
    std::size_t pressureSpawnCount{};
    std::array<Vec2, 8> pressureSpawnCenters{};
    float nextWaveSeconds{};
    bool detailsKnown{};
    bool active{};
};

// 当前会话组合根。ProfileState 是新版 Base 的跨进程权威状态；旧 Stash、
// GameplayWorld 与 RaidSettlement 仅作为隔离的 V0 Raid 适配器保留。
// 本类型不负责 SDL 输入或渲染。
class GameSession
{
public:
    GameSession();

    explicit GameSession(
        InventoryGridSize stashSize);

    // Integration tests can isolate a long non-combat settlement slice with
    // an explicit first-raid deployment. Shipped sessions use the default.
    explicit GameSession(
        std::vector<EnemySpawn> firstRaidEnemies);

    void update(
        const GameplayInput &input,
        float deltaTime);

    [[nodiscard]] std::optional<BaseFacilityKind> updateBaseWorld(
        BaseWorld &baseWorld,
        const GameplayInput &input,
        float deltaTime);

    // 只有完整结算后才能开始下一局。候选世界完整构造成功后才交换，
    // 因此失败不会破坏旧终局、Stash、结算或 Raid 编号。
    [[nodiscard]]
    bool startNextRaid() noexcept;

    [[nodiscard]]
    GameplayWorld &world() noexcept;

    [[nodiscard]]
    const GameplayWorld &world() const noexcept;

    [[nodiscard]]
    Stash &stash() noexcept;

    [[nodiscard]]
    const Stash &stash() const noexcept;

    [[nodiscard]]
    const RaidSettlement &settlement() const noexcept;

    [[nodiscard]] std::uint64_t currentRaidCarriedWeightGrams() const noexcept;

    [[nodiscard]] std::uint64_t
    conditionalExtractionWeightLimitGrams() const noexcept;

    [[nodiscard]] bool conditionalExtractionEligible() const noexcept;

    [[nodiscard]]
    GameSessionState state() const noexcept;

    [[nodiscard]]
    bool canStartNextRaid() const noexcept;

    [[nodiscard]]
    std::size_t raidNumber() const noexcept;

    [[nodiscard]]
    ItemInstanceId nextItemInstanceId() const noexcept;

    void configurePersistence(std::filesystem::path directory);

    [[nodiscard]] bool hasSavedProfile() const;
    [[nodiscard]] bool startNewProfile(std::string profileId);
    [[nodiscard]] bool continueProfile();

    void advanceBaseWorldClock(float deltaTime);
    [[nodiscard]] bool checkpointWorldClock();
    [[nodiscard]] WorldClockProjection worldClockProjection() const noexcept;
    [[nodiscard]] BaseThreatProjection baseThreatProjection() const noexcept;
    [[nodiscard]] BaseAutoDefensePlan baseAutoDefensePlan() const noexcept;
    [[nodiscard]] BaseAutoDefenseReceipt executeBaseAutoDefense(
        std::string transactionId);
    [[nodiscard]] std::optional<RaidTravelPreview> raidTravelPreview(
        const MapDefinitionId &mapDefinitionId) const noexcept;
    [[nodiscard]] std::vector<LostRaidRecordProjection>
    lostRaidRecordProjections() const;
    [[nodiscard]] LostRaidAgingPreview lostRaidAgingPreview() const noexcept;
    [[nodiscard]] RecoveryTaskQuote recoveryTaskQuote(
        const std::string &recordId) const;
    [[nodiscard]] std::optional<RecoveryTaskProjection>
    recoveryTaskProjection() const;
    [[nodiscard]] RecoveryTaskReceipt startRecoveryTask(
        const std::string &recordId,
        std::string transactionId);
    [[nodiscard]] RecoveryTaskReceipt cancelRecoveryTask(
        std::string transactionId);
    [[nodiscard]] RecoveryTaskReceipt collectRecoveryTask(
        std::string transactionId);

    [[nodiscard]] bool deployAlpha(
        std::uint64_t seed,
        MapDefinitionId mapDefinitionId = MapDefinitionId{"map.v0.test"},
        RaidIntelligenceLoadout intelligence = {},
        std::optional<std::string> selfRecoveryRecordId = std::nullopt,
        std::optional<RegionalOutpostDefinitionId>
            outpostRestorationId = std::nullopt,
        std::optional<RegionalBaseSiteDefinitionId>
            baseSiteClearanceId = std::nullopt,
        std::optional<RegionalBaseSiteDefinitionId>
            basePerimeterSweepId = std::nullopt,
        const RaidDeploymentProgressCallback &progress = {});
    [[nodiscard]] bool outpostRestorationObjectiveSecured() const noexcept;
    [[nodiscard]] bool baseSiteClearanceObjectiveSecured() const noexcept;
    [[nodiscard]] bool basePerimeterSweepObjectiveSecured() const noexcept;
    [[nodiscard]] RaidOperationProjection
    raidOperationProjection() const noexcept;
    [[nodiscard]] std::optional<RaidHighRiskCrisisProjection>
    raidHighRiskCrisisProjection() const noexcept;
    [[nodiscard]] bool triggerDeveloperHighRisk() noexcept;
    [[nodiscard]] std::optional<RaidSelfRecoveryProjection>
    raidSelfRecoveryProjection() const noexcept;
    [[nodiscard]] RaidIntelligencePurchaseReceipt
    purchaseRaidIntelligence(
        const RaidIntelligencePurchaseCommand &command,
        std::string transactionId);
    [[nodiscard]] RaidInteriorIntelligencePurchaseReceipt
    purchaseRaidInteriorIntelligence(
        const RaidInteriorIntelligencePurchaseCommand &command,
        std::string transactionId);
    [[nodiscard]] bool activeQuitAlphaRaid();
    [[nodiscard]] bool startAlphaReload(
        AssetInstanceId weaponAssetId,
        AssetInstanceId magazineAssetId);
    [[nodiscard]] bool startAlphaLoadMagazine(
        AssetInstanceId ammunitionAssetId,
        AssetInstanceId magazineAssetId,
        std::uint32_t quantity);
    [[nodiscard]] bool startAlphaUnloadMagazine(
        AssetInstanceId magazineAssetId);
    [[nodiscard]] bool startAlphaHeal(AssetInstanceId medkitAssetId);
    [[nodiscard]] bool startAlphaMedical(AssetInstanceId medicalAssetId);
    [[nodiscard]] bool startAlphaWeaponMaintenance(
        AssetInstanceId kitAssetId,
        AssetInstanceId weaponAssetId);
    [[nodiscard]] bool startAlphaArmorMaintenance(
        AssetInstanceId kitAssetId,
        AssetInstanceId armorAssetId);
    [[nodiscard]] bool startAlphaWeaponSwitch(EquipmentSlotKind targetSlot);
    [[nodiscard]] bool observeAlphaWeaponClearMotion(Vec2 delta);
    [[nodiscard]] bool alphaRaidActive() const noexcept;
    [[nodiscard]] bool recoveredAbandonedRaid() const noexcept;

    [[nodiscard]] bool
    raidLootAccessible(const RaidLootSnapshot &loot) const noexcept;

    [[nodiscard]] const ProfileState &profile() const noexcept;
    [[nodiscard]] EquipmentSlotKind activeAlphaWeaponSlot() const noexcept;
    [[nodiscard]] std::optional<AssetInstanceId>
    activeAlphaWeapon() const noexcept;

    [[nodiscard]] std::optional<DeveloperWeaponTuningSnapshot>
    developerWeaponTuning() const;
    [[nodiscard]] bool adjustDeveloperWeaponTuning(
        DeveloperWeaponParameter parameter,
        int direction,
        bool coarseStep);
    [[nodiscard]] bool resetDeveloperWeaponTuning();
    [[nodiscard]] WarehouseCatalogGrantReceipt
    grantDeveloperWarehouseCatalog();
    [[nodiscard]] bool developerWarehouseCatalogGranted() const noexcept;

    [[nodiscard]] InventoryReceipt executeProfileInventory(
        const InventoryCommand &command,
        std::string transactionId);

    [[nodiscard]] BaseGroundReceipt executeBaseGroundAsset(
        const BaseGroundCommand &command,
        std::string transactionId);

    [[nodiscard]] BaseFacilityLayoutReceipt executeBaseFacilityLayout(
        const RepositionBaseFacilityCommand &command,
        std::string transactionId);

    [[nodiscard]] BaseFacilityLayoutReceipt executeInstallBaseFacilityAt(
        const InstallBaseFacilityAtCommand &command,
        std::string transactionId);

    [[nodiscard]] InventoryReceipt executeBaseGroundContainerInventory(
        AssetInstanceId containerAssetId,
        const BaseGroundAccess &access,
        const InventoryCommand &command,
        std::string transactionId);

    [[nodiscard]] EconomyReceipt executeProfileEconomy(
        const EconomyCommand &command,
        std::string transactionId);

    struct BasePlaceablePurchaseReceipt
    {
        bool succeeded{};
        DomainErrorCode error{DomainErrorCode::None};
        std::string message;
        ProfileRevision revision{};
        AssetInstanceId assetId{};
        std::int64_t currencyDelta{};
    };

    [[nodiscard]] BasePlaceablePurchaseReceipt purchaseBasePlaceable(
        ItemDefinitionId definitionId,
        std::string transactionId);

    [[nodiscard]] BaseResourceReceipt executeBaseResourceContribution(
        AssetInstanceId assetId,
        std::string transactionId);

    [[nodiscard]] BaseSupplyAssignmentReceipt executeBaseSupplyAssignment(
        ItemDefinitionId definitionId,
        std::optional<BaseSupplyCategory> category,
        std::string transactionId);

    [[nodiscard]] ConstructionMaterialReceipt
    executeConstructionMaterialContribution(
        AssetInstanceId assetId,
        std::string transactionId);

    [[nodiscard]] BaseConstructionReceipt executeStartBaseConstruction(
        BaseConstructionProjectDefinitionId definitionId,
        std::string transactionId);

    [[nodiscard]] BaseConstructionReceipt executeCancelBaseConstruction(
        BaseConstructionProjectDefinitionId definitionId,
        std::string transactionId);

    [[nodiscard]] BaseMigrationReceipt executeBaseMigration(
        RegionalBaseSiteDefinitionId targetSiteDefinitionId,
        std::string transactionId);

    [[nodiscard]] BaseSiteFeatureRepairReceipt executeBaseSiteFeatureRepair(
        RegionalBaseSiteDefinitionId siteDefinitionId,
        std::string transactionId);

    [[nodiscard]] InstallBaseFacilityReceipt executeInstallBaseFacility(
        BaseFacilityDefinitionId definitionId,
        std::string transactionId);

    [[nodiscard]] RegionalOutpostReceipt executeEstablishRegionalOutpost(
        RegionalOutpostDefinitionId definitionId,
        std::string transactionId);

    [[nodiscard]] RegionalOutpostStaffingReceipt
    executeRegionalOutpostStaffing(
        RegionalOutpostDefinitionId definitionId,
        bool assign,
        std::string transactionId);

    [[nodiscard]] BaseWorkforceReceipt executeAssignBestBaseWorker(
        BaseFacilityStaffingKind facility,
        std::string transactionId);
    [[nodiscard]] BaseWorkforceReceipt executeClearBaseWorker(
        BaseFacilityStaffingKind facility,
        std::string transactionId);
    [[nodiscard]] BaseWorkforceReceipt executeAutoFillBaseWorkers(
        std::string transactionId);

    [[nodiscard]] BasePriorityReceipt executeBasePrioritySubmission(
        AssetInstanceId assetId,
        std::string transactionId);

    [[nodiscard]] BaseRestReceipt executeBaseRest(
        std::uint32_t hours,
        std::string transactionId);

    [[nodiscard]] BaseManufacturingReceipt executeStartBaseManufacturing(
        BaseManufacturingRecipeDefinitionId definitionId,
        std::string transactionId);
    [[nodiscard]] BaseManufacturingReceipt executeCancelBaseManufacturing(
        std::string transactionId);
    [[nodiscard]] BaseManufacturingReceipt executeCollectBaseManufacturing(
        std::string transactionId);

    [[nodiscard]] WeaponAmmoReceipt executeProfileWeaponAmmo(
        const WeaponAmmoCommand &command,
        std::string transactionId);

    [[nodiscard]] HealReceipt executeBaseHeal(
        AssetInstanceId medkitAssetId,
        std::string transactionId);
    [[nodiscard]] MedicalUseReceipt executeBaseMedical(
        AssetInstanceId medicalAssetId,
        std::string transactionId);
    [[nodiscard]] BaseMedicalServiceReceipt executeBasePaidMedicalService(
        std::string transactionId);
    [[nodiscard]] ResidentTreatmentReceipt executeStartResidentTreatment(
        std::string transactionId);
    [[nodiscard]] WeaponMaintenanceReceipt executeBaseWeaponMaintenance(
        AssetInstanceId kitAssetId,
        AssetInstanceId weaponAssetId,
        std::string transactionId);
    [[nodiscard]] ArmorMaintenanceReceipt executeBaseArmorMaintenance(
        AssetInstanceId kitAssetId,
        AssetInstanceId armorAssetId,
        std::string transactionId);
    [[nodiscard]] GunsmithMaintenanceReceipt executeBaseGunsmithMaintenance(
        AssetInstanceId weaponAssetId,
        std::string transactionId);
    [[nodiscard]] GunsmithCollectionReceipt collectBaseGunsmithMaintenance(
        std::string transactionId);

    [[nodiscard]] const RaidActionState &raidActionState() const noexcept;
    [[nodiscard]] const std::optional<CombatDamageResolution> &
    lastIncomingDamage() const noexcept;

    [[nodiscard]] std::vector<GameSessionPresentationEvent>
    takePresentationEvents();

    [[nodiscard]] SaveLoadStatus lastSaveLoadStatus() const noexcept;
    [[nodiscard]] const std::string &persistenceMessage() const noexcept;
    [[nodiscard]] std::optional<OrdinarySurvivorAdmissionPlan>
    ordinarySurvivorRescuePlan() const;

    void noteBaseFacility(BaseFacilityKind facility);

private:
    ProfileState profile_;
    std::optional<SaveRepository> saveRepository_;
    std::optional<ProfileState> activeRaidRecoveryProfile_;
    SaveLoadStatus lastSaveLoadStatus_{SaveLoadStatus::NotFound};
    std::string persistenceMessage_;
    Stash stash_;
    std::unique_ptr<GameplayWorld> world_;
    RaidSettlement settlement_;
    GameSessionState state_{GameSessionState::InRaid};
    std::size_t raidNumber_{1};
    bool alphaRaidActive_{};
    bool recoveredAbandonedRaid_{};
    RaidActionState raidActionState_;
    std::optional<CombatDamageResolution> lastIncomingDamage_;
    std::vector<GameSessionPresentationEvent> presentationEvents_;
    std::vector<GameSessionPresentationEvent>
        deferredRaidBasePresentationEvents_;
    std::uint64_t raidCommandSequence_{};
    std::uint64_t medicalRandomSequence_{};
    std::uint64_t woundRandomSequence_{};
    std::uint64_t weaponFaultSequence_{};
    float raidElapsedSeconds_{};
    float selfRecoveryInteractionSeconds_{};
    bool outpostRestorationObjectiveSecured_{};
    bool baseSiteClearanceObjectiveSecured_{};
    bool basePerimeterSweepObjectiveSecured_{};
    double pendingWorldSeconds_{};
    float worldClockCheckpointElapsedSeconds_{};
    float baseSiegeWarningSecondAccumulator_{};
    bool worldClockDirty_{};
    WeaponClearGesture weaponClearGesture_;
    float medicalTickAccumulatorSeconds_{};
    bool fireSuppressedUntilRelease_{};
    bool sprintSuppressedUntilRelease_{};
    bool sprintFireIntentPending_{};
    float sprintFireReadyRemaining_{};
    EquipmentSlotKind activeWeaponSlot_{EquipmentSlotKind::PrimaryWeapon};
    std::optional<AssetInstanceId> configuredWeaponAssetId_;
    std::optional<AssetInstanceId> configuredBaseWeaponAssetId_;
    float baseCombatElapsedSeconds_{};

    struct DeveloperWeaponHiddenOverrides
    {
        std::optional<float> maximumReticleSpeed;
        std::optional<float> reticleControlAcceleration;
        std::optional<float> spreadPerShotDegrees;
        std::optional<float> recoilLateralRatio;
        std::optional<float> recoilBendDurationSeconds;
        std::optional<float> movingSpreadFraction;
        std::optional<float> sprintingSpreadFraction;
        std::optional<float> reticleMotionSpreadDegreesPerSecond;
        std::optional<float> nearDistanceSpreadScale;
        std::optional<float> distanceBloomAtEffectiveRange;
        std::optional<float> adsAccuracyMultiplier;
        std::optional<float> adsStabilityMultiplier;
        std::optional<float> weakTracerLength;
        std::optional<float> weakTracerOpacity;
        std::optional<float> weakTracerLifetimeSeconds;

        friend bool operator==(
            const DeveloperWeaponHiddenOverrides &,
            const DeveloperWeaponHiddenOverrides &) = default;
    };

    struct DeveloperWeaponOverride
    {
        AssetInstanceId weaponAssetId{};
        WeaponUseDefinition weaponUse;
        DeveloperWeaponHiddenOverrides hidden;

        friend bool operator==(
            const DeveloperWeaponOverride &,
            const DeveloperWeaponOverride &) = default;
    };

    std::vector<DeveloperWeaponOverride> developerWeaponOverrides_;

    [[nodiscard]] bool commitProfileCandidate(
        ProfileState candidate,
        bool persist = true);
    void refreshLoadoutTutorial();
    void advanceWorldClockFromSimulation(
        float deltaTime,
        bool allowPeriodicCheckpoint);
    void advanceBaseSiegeFromSimulation(float deltaTime);
    void resetWorldClockRuntime() noexcept;
    void recordBaseCompletionEvents(
        const BaseConstructionAdvanceResult &construction,
        const BaseManufacturingAdvanceResult &manufacturing,
        const ResidentTreatmentAdvanceResult &residentTreatment,
        bool deferUntilRaidSettlement);
    void publishDeferredRaidBasePresentationEvents();
    void updateAlphaRaid(const GameplayInput &input, float deltaTime);
    void applyAlphaIncomingDamage();
    void advanceAlphaMedicalStatus(float deltaTime);
    void synchronizeActiveAlphaWeapon();
    void synchronizeActiveBaseWeapon(BaseWorld &baseWorld);
    [[nodiscard]] std::optional<std::size_t>
    developerWeaponOverrideIndex(AssetInstanceId weaponAssetId) const noexcept;
    [[nodiscard]] WeaponHandlingParameters effectiveDeveloperHandling(
        const DeveloperWeaponOverride &override) const noexcept;
    [[nodiscard]] bool advanceContinuousHealing(
        MedicalRaidAction &action,
        float deltaTime);
    [[nodiscard]] bool settleAlphaRaid(RaidResultOutcome outcome);
    [[nodiscard]] bool secureOrdinarySurvivorRescue();
    [[nodiscard]] std::string nextRaidTransaction(std::string_view prefix);
    [[nodiscard]] std::optional<AssetInstanceId> nearbyRaidLoot() const;
    [[nodiscard]] bool selfRecoveryCacheInRange() const noexcept;
};

[[nodiscard]]
const char *gameSessionStateName(
    GameSessionState state) noexcept;
