#pragma once

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
#include "world_clock.h"

using AssetInstanceId = std::uint64_t;
using ProfileRevision = std::uint64_t;
using BaseServiceJobId = std::uint64_t;

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

using AssetLocation = std::variant<
    StoredAssetLocation,
    EquippedAssetLocation,
    InstalledMagazineLocation,
    RaidGroundAssetLocation,
    BaseServiceAssetLocation>;

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

struct RaidEnemySnapshot
{
    Vec2 position{};
    Vec2 size{50.0F, 50.0F};
    int maximumHealth{};
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

struct RaidRescueSnapshot
{
    RescueDefinitionId definitionId;
    RaidRescueSubjectKind subjectKind{RaidRescueSubjectKind::OrdinaryResidents};
    ContentRect transferPoint;
    float interactionDurationSeconds{};
    std::uint32_t ordinaryResidentCount{};
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

    friend bool operator==(
        const BasePopulationState &,
        const BasePopulationState &) = default;
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
    BaseConstructionState startingBaseConstruction;
    std::uint32_t startingBedCapacity{10};

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
    std::vector<AssetInstanceId> carriedRootAssetIds;
    int startingHealth{100};
    MedicalStatusState startingMedicalStatus;
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
    BasePopulationState basePopulation;
    BaseConstructionState baseConstruction;
    BasePriorityState basePriority;
    BaseServiceJobId nextBaseServiceJobId{1};
    std::optional<GunsmithMaintenanceJob> gunsmithMaintenanceJob;
    AssetRegistry assets;
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
