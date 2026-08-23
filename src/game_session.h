#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "economy_domain.h"
#include "gameplay_world.h"
#include "inventory_domain.h"
#include "profile_combat_domain.h"
#include "raid_action.h"
#include "raid_lifecycle.h"
#include "raid_settlement.h"
#include "save_repository.h"
#include "stash.h"
#include "maintenance_domain.h"
#include "weapon_clear_gesture.h"

enum class BaseFacilityKind;

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
};

struct DeveloperWeaponTuningSnapshot
{
    AssetInstanceId weaponAssetId{};
    ItemDefinitionId weaponDefinitionId;
    WeaponUseDefinition weaponUse;
    WeaponHandlingParameters handling;
    bool overridden{};
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

    [[nodiscard]] bool deployAlpha(
        std::uint64_t seed,
        MapDefinitionId mapDefinitionId = MapDefinitionId{"map.v0.test"});
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

    [[nodiscard]] InventoryReceipt executeProfileInventory(
        const InventoryCommand &command,
        std::string transactionId);

    [[nodiscard]] EconomyReceipt executeProfileEconomy(
        const EconomyCommand &command,
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
    [[nodiscard]] WeaponMaintenanceReceipt executeBaseWeaponMaintenance(
        AssetInstanceId kitAssetId,
        AssetInstanceId weaponAssetId,
        std::string transactionId);
    [[nodiscard]] ArmorMaintenanceReceipt executeBaseArmorMaintenance(
        AssetInstanceId kitAssetId,
        AssetInstanceId armorAssetId,
        std::string transactionId);

    [[nodiscard]] const RaidActionState &raidActionState() const noexcept;
    [[nodiscard]] const std::optional<CombatDamageResolution> &
    lastIncomingDamage() const noexcept;

    [[nodiscard]] std::vector<GameSessionPresentationEvent>
    takePresentationEvents();

    [[nodiscard]] SaveLoadStatus lastSaveLoadStatus() const noexcept;
    [[nodiscard]] const std::string &persistenceMessage() const noexcept;

    void noteBaseFacility(BaseFacilityKind facility);

private:
    ProfileState profile_;
    std::optional<SaveRepository> saveRepository_;
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
    std::uint64_t raidCommandSequence_{};
    std::uint64_t medicalRandomSequence_{};
    std::uint64_t woundRandomSequence_{};
    std::uint64_t weaponFaultSequence_{};
    float raidElapsedSeconds_{};
    WeaponClearGesture weaponClearGesture_;
    float medicalTickAccumulatorSeconds_{};
    bool fireSuppressedUntilRelease_{};
    bool sprintSuppressedUntilRelease_{};
    bool sprintFireIntentPending_{};
    float sprintFireReadyRemaining_{};
    EquipmentSlotKind activeWeaponSlot_{EquipmentSlotKind::PrimaryWeapon};
    std::optional<AssetInstanceId> configuredWeaponAssetId_;

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
    void updateAlphaRaid(const GameplayInput &input, float deltaTime);
    void applyAlphaIncomingDamage();
    void advanceAlphaMedicalStatus(float deltaTime);
    void synchronizeActiveAlphaWeapon();
    [[nodiscard]] std::optional<std::size_t>
    developerWeaponOverrideIndex(AssetInstanceId weaponAssetId) const noexcept;
    [[nodiscard]] WeaponHandlingParameters effectiveDeveloperHandling(
        const DeveloperWeaponOverride &override) const noexcept;
    [[nodiscard]] bool advanceContinuousHealing(
        MedicalRaidAction &action,
        float deltaTime);
    [[nodiscard]] bool settleAlphaRaid(RaidResultOutcome outcome);
    [[nodiscard]] std::string nextRaidTransaction(std::string_view prefix);
    [[nodiscard]] std::optional<AssetInstanceId> nearbyRaidLoot() const;
};

[[nodiscard]]
const char *gameSessionStateName(
    GameSessionState state) noexcept;
