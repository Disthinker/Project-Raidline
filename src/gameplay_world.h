#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "enemy.h"
#include "enemy_squad.h"
#include "extraction_point.h"
#include "gameplay_input.h"
#include "grid_inventory.h"
#include "ground_item.h"
#include "loot_table.h"
#include "raid_corpse_search.h"
#include "raid_credential_extraction.h"
#include "raid_fire_support_runtime.h"
#include "particle_system.h"
#include "player.h"
#include "player_damage_observation.h"
#include "grenade_explosion_plan.h"
#include "logical_ballistics.h"
#include "raid_session.h"
#include "raid_map_generation.h"
#include "raid_space_query.h"
#include "raid_space_spatial_index.h"
#include "raid_tactical_map.h"
#include "shot_resolution.h"
#include "shot_feedback_presentation.h"
#include "storage_cabinet.h"
#include "weapon_fire.h"
#include "weapon_aim.h"
#include "world_shooting_runtime.h"
#include "world_melee_runtime.h"
#include "raid_noise_decoy_runtime.h"
#include "raid_grenade_runtime.h"
#include "thrown_illumination_runtime.h"
#include "raid_smoke_runtime.h"
#include "raid_fire_zone_runtime.h"
#include "raid_proximity_alarm.h"
#include <deque>
#include "raid_volatile_barrel.h"

enum class RaidSquadRole;

struct RaidSimulationWorkload
{
    std::size_t activeEnemies{};
    std::size_t activeBlockers{};
    std::size_t enemySubsteps{};
    std::size_t navigationQueries{};
    std::size_t navigationRefreshesDeferred{};
    std::size_t neighborCandidatesExamined{};
    std::size_t lineOfSightBlockersExamined{};
    std::size_t movementBlockersExamined{};
};
#include "content_registry.h"
#include "hit_resolution.h"
#include "medical_types.h"

// 用于配置 GameplayWorld 初始地面物品。
// GameplayWorld 根据这些定义自行生成稳定 instanceId。
struct GroundItemSpawn
{
    ItemId definitionId{};
    Vec2 position{};
    std::uint32_t quantity{1};
};

struct RaidArmedScavengerConfig
{
    FireSupportConfig pistol;
    int loadedRounds{};
    int spareRounds{};
    bool spareMagazineInstalled{};
    std::optional<Vec2> retreatPoint;
};

struct RaidArmedScavengerView
{
    std::string sourceEnemyId;
    CombatTargetId id{};
    int loadedRounds{}, spareRounds{};
    bool spareMagazineInstalled{};
    int shotsFired{};
    bool alive{true}, aiming{}, reloading{};
    Vec2 aimDirection{};
    bool ammunitionChangedLastUpdate{};
    bool retreating{}, escapeReady{}, escaped{};
    Vec2 retreatDirection{};
};

struct EnemySpawn
{
    Vec2 position{};
    Vec2 size{50.0F, 50.0F};
    int maxHealth{3};
    std::string encounterGroupInstanceId;
    RaidEncounterKind encounterKind{RaidEncounterKind::Guard};
    Vec2 encounterHome{};
    std::vector<Vec2> patrolPoints;
    float ambushActivationDistance{240.0F};
    std::optional<EnemyArmorState> torsoArmor;
    std::string sourceEnemyId;
    std::optional<EnemyVariantSnapshot> variant;
    bool lostWeaponCarrier{};
    std::optional<RaidArmedScavengerConfig> armedScavenger;
};

struct HighRiskWorldConfig
{
    bool enabled{};
    float regularPhaseDurationSeconds{};
    ContentRect emergencyExtractionPoint;
    float emergencyExtractionDurationSeconds{};
    float initialWaveDelaySeconds{};
    float waveIntervalSeconds{};
    std::uint32_t waveSize{};
    std::uint32_t activeEnemyCap{};
    std::vector<EnemySpawn> pressureSpawns;
    ContentRect activationControlPoint;
    float activationDurationSeconds{};
    ContentRect advancedResourceArea;
    std::uint64_t seed{};
    ContentRect conditionalExtractionPoint;
    float conditionalExtractionDurationSeconds{};
    std::uint64_t conditionalExtractionMaximumWeightGrams{};
    bool fuelExtraction{};
};

struct RaidInteriorWorldConfig
{
    RaidSpaceDefinitionId id;
    std::string displayName;
    bool layoutKnown{};
    Vec2 worldSize{};
    ContentRect exteriorEntrance;
    Vec2 exteriorReturn{};
    Vec2 interiorSpawn{};
    ContentRect interiorExit;
    std::vector<EnemySpawn> initialEnemies;
    std::vector<BallisticBlocker> ballisticBlockers;
    RaidLightEnvironment lightEnvironment{RaidLightEnvironment::WeakIndoor};
};

struct RaidInteriorMapProjection
{
    RaidSpaceDefinitionId id;
    std::string_view displayName;
    Vec2 worldSize{};
    ContentRect exit;
    std::span<const BallisticBlocker> blockers;
    // Only this rectangle may be drawn; unknown layouts reveal no exploration history.
    ContentRect visibleBounds;
    bool layoutKnown{};
};

struct RaidSpacePortalProjection
{
    RaidSpaceDefinitionId id;
    std::string_view displayName;
    ContentRect bounds;
    bool returnsOutside{};
    bool interactionInRange{};
};

enum class RaidFieldTradeAccess
{
    Unavailable,
    OutOfRange,
    Obstructed,
    Threatened,
    Available
};

enum class HospitalEscortState { AwaitingContact, Following, Waiting, Down, Delivered };

struct HospitalEscortView
{
    Vec2 position;
    ContentRect handoff;
    int health{};
    HospitalEscortState state{HospitalEscortState::AwaitingContact};
    bool outdoor{}, visible{}, inInteractionRange{}, threatened{}, atHandoff{};
};

struct RaidWorldConfig
{
    Vec2 worldSize{1280.0F, 720.0F};
    Vec2 playerSpawn{};
    ContentRect extractionPoint;
    std::vector<EnemySpawn> initialEnemies;
    int playerMaximumHealth{100};
    int playerCurrentHealth{100};
    bool deferPlayerDamageResolution{};
    std::vector<BallisticBlocker> ballisticBlockers;
    RaidGeneratedMapLayout outdoorLayout;
    std::uint32_t outdoorColumns{};
    std::uint32_t outdoorRows{};
    std::uint32_t outdoorChunkSizeCells{};
    std::vector<RaidInteriorWorldConfig> interiors;
    float normalExtractionDurationSeconds{3.0F};
    HighRiskWorldConfig highRisk;
    struct OrdinarySurvivorRescue
    {
        ContentRect transferPoint;
        float interactionDurationSeconds{};
        std::optional<Vec2> escortStart;
    };
    std::optional<OrdinarySurvivorRescue> rescue;
    RaidIntelligenceLoadout intelligence;
    std::vector<RaidTacticalObjective> tacticalObjectives;
    bool developerRange{};
    bool noiseDecoyEnabled{};
    bool lightConeObservationEnabled{};
    std::optional<RaidCredentialExtractionSnapshot> credentialExtraction;
    bool grenadeEnabled{};
    bool freightBreachEnabled{};
    std::optional<Vec2> fieldIntelligenceContact;
    std::optional<RaidRoadBlockadeSnapshot> roadBlockade;
    std::optional<Vec2> lostWeaponPursuitFallback;
    std::optional<ContentRect> nightRendezvous;
    bool nightRendezvousUnavailable{};
    std::optional<FireSupportConfig> fireSupport;
    std::vector<RaidVolatileBarrelSpawn> volatileBarrels;
    std::array<std::optional<FireSupportConfig>, 3> squadWeapons{};
    std::array<int, 3> squadInitialHealth{100, 100, 100};
};

struct RaidNightRendezvousView
{
    ContentRect bounds;
    bool visible{};
    bool discovered{};
    bool inRange{};
    bool inProgress{};
    bool enemyInRange{};
    bool interactionBlocked{};
    bool ready{};
    bool secured{};
    bool signalSent{};
    float progress{};
    float remainingSeconds{kRaidNightRendezvousDurationSeconds};
};

struct RaidRoadBlockadeView
{
    RaidRoadBlockadePhase phase{RaidRoadBlockadePhase::Preparing};
    ContentRect bounds;
    float phaseSecondsRemaining{};
    bool awaitingClear{};
    bool nearby{};
    bool canInteract{};
    bool visible{};
};

struct RaidOutdoorLabelProjection
{
    std::string text;
    Vec2 position{};
    bool landmark{};
};

struct RaidOutdoorPresentationProjection
{
    std::vector<RaidTerrainSpan> terrainSpans;
    std::vector<RaidOutdoorRoadCell> roadCells;
    std::vector<RaidOutdoorPropSnapshot> props;
    std::vector<RaidResourcePointSnapshot> resourcePoints;
    std::vector<RaidOutdoorLabelProjection> labels;
    std::size_t queriedChunkCount{};
    std::uint64_t cacheRevision{};
};

class GameplayWorld
{
    friend struct NoiseDecoyTestAccess;
public:
    [[nodiscard]] Vec2 playerWorldSize() const noexcept { return activeWorldSize(); }
    struct TrainingCorpseDeathFact { CombatTargetId id{}; Vec2 center{},size{}; };
    [[nodiscard]] const std::vector<TrainingCorpseDeathFact> &trainingCorpseDeaths() const noexcept { return trainingCorpseDeaths_; }
    void acknowledgeTrainingCorpseDeaths() noexcept { trainingCorpseDeaths_.clear(); }
    GameplayWorld();
    [[nodiscard]] bool resetDeveloperRangeEnemies(const EnemyCombatDefinition &definition,
        std::size_t pad, std::size_t count);

    // 可重复 Raid 会话传入本局第一个未使用的稳定 ID。
    explicit GameplayWorld(
        ItemInstanceId firstItemInstanceId);

    // 为 GameplayWorldTest 提供最小 Enemy HP 配置入口。
    // 正常游戏仍使用默认的 3 HP Enemy。
    explicit GameplayWorld(int enemyMaxHealth);

    // Allows integration tests to keep a player alive after Scratch + Bite
    // without changing the shipped three-HP balance.
    GameplayWorld(
        int enemyMaxHealth,
        int playerMaxHealth);

    // Week28 deterministic multi-enemy integration tests can provide an
    // explicit deployment without mutating the owned vector after start.
    GameplayWorld(
        std::vector<EnemySpawn> initialEnemies,
        int playerMaxHealth);

    explicit GameplayWorld(RaidWorldConfig config);

    // 使用默认 10×6 背包。
    GameplayWorld(
        int enemyMaxHealth,
        std::vector<GroundItemSpawn> initialGroundItems);

    // 允许测试使用较小背包快速验证容量边界。
    GameplayWorld(
        int enemyMaxHealth,
        std::vector<GroundItemSpawn> initialGroundItems,
        InventoryGridSize inventorySize);

    GameplayWorld(
        int enemyMaxHealth,
        std::vector<GroundItemSpawn> initialGroundItems,
        InventoryGridSize inventorySize,
        ItemInstanceId firstItemInstanceId);

    using RaidFuelSelector = std::function<std::optional<AssetInstanceId>(std::optional<AssetInstanceId>)>;

    void update(
        const GameplayInput &input,
        float deltaTime,
        const std::function<bool()> &commitGrenade = {},
        const std::function<bool()> &beforeSpaceTransition = {},
        bool portalInteractionRequested = false,
        const RaidFuelSelector &selectFuel = {});

    [[nodiscard]]
    const Player &player() const;
    [[nodiscard]] const std::vector<RaidCorpseDeathFact> &pendingCorpseDeaths() const noexcept { return pendingCorpseDeaths_; }
    void acknowledgeCorpseDeath(std::string_view sourceId);
    [[nodiscard]] std::optional<Vec2> lostWeaponCarrierDeath() const noexcept;

    [[nodiscard]]
    // Test-only visibility into non-entity logical flight records. App and
    // domain consumers use shot projections/results instead.
    const std::vector<LogicalBallisticFlight> &
    logicalBallistics() const;

    // App consumes this read-only projection. It has no collision or damage
    // authority and only describes the already-travelled presentation point.
    [[nodiscard]]
    std::vector<ShotPresentationSnapshot>
    shotPresentationSnapshots() const;

    // Accepted-shot-only presentation. These snapshots and the normalized
    // camera offset have no collision, damage, aiming, or persistence role.
    [[nodiscard]] std::vector<ShotFeedbackPresentationSnapshot>
    shotFeedbackPresentationSnapshots() const;

    [[nodiscard]] Vec2 normalizedShotScreenShakeOffset() const noexcept;

    [[nodiscard]]
    const std::vector<Enemy> &
    enemies() const;

    [[nodiscard]]
    const std::vector<BallisticBlocker> &
    ballisticBlockers() const noexcept;
    [[nodiscard]] std::optional<EnemyLowLungeCorridor>
    lowLungeTelegraph(const Enemy &enemy) const noexcept;

    [[nodiscard]] const RaidSpaceDefinitionId &
    activeRaidSpaceId() const noexcept;
    [[nodiscard]] bool inOutdoorRaidSpace() const noexcept;
    [[nodiscard]] std::string_view
    activeRaidSpaceDisplayName() const noexcept;
    [[nodiscard]] Vec2 raidSpaceWorldSize() const noexcept;
    [[nodiscard]] const RaidVisibilityProjection &indoorObservation() const;
    [[nodiscard]] const RaidVisibilityProjection &raidObservation() const { return indoorObservation(); }
    [[nodiscard]] RaidObservationKey raidObservationKey() const;
    [[nodiscard]] bool lightConeObservationEnabled() const noexcept
    { return lightConeObservationEnabled_ || activeWeaponOptic() != WeaponOpticMode::None; }
    void setRaidDaylight(bool day) noexcept { raidDaylight_ = day; }
    [[nodiscard]] bool nightVisionSupported() const noexcept { return lightConeObservationEnabled_; }
    [[nodiscard]] bool isDarkForNightVision() const noexcept
    { return currentObservationEnvironment() != RaidLightEnvironment::DayOutdoor; }
    void setNightVision(RaidSightParameters outdoor, RaidSightParameters indoor) noexcept;
    void setWeaponLight(std::uint64_t device, std::uint64_t battery, float angle, float distance, float backgroundRadius) noexcept;
    [[nodiscard]] float weaponLightBackgroundRadius() const noexcept { return weaponLightBackgroundRadius_; }
    [[nodiscard]] const std::array<RaidVisibilityProjection,2> &weaponLightBackgroundObservations() const
    { static_cast<void>(raidObservation()); return weaponLightBackgroundCache_; }
    [[nodiscard]] const std::array<RaidVisibilityProjection, 2> &
        thrownIlluminationBackgroundObservations() const;
    [[nodiscard]] float thrownIlluminationGlareSeconds() const noexcept;
    [[nodiscard]] RaidObservationWork observationWork() const noexcept { return observationWork_; }
    [[nodiscard]] bool raidBoundsObservable(Rect bounds) const;
    [[nodiscard]] bool raidEnemyObservable(CombatTargetId id) const;
    [[nodiscard]] std::optional<Rect> visibleLostWeaponCarrier() const;
    [[nodiscard]] bool lostWeaponCorpseObservable() const;
    [[nodiscard]] std::uint64_t enemyObservationSolveCount() const noexcept { return enemyObservationSolveCount_; }
    [[nodiscard]] std::uint64_t indoorObservationBuildCount() const noexcept { return indoorObservationBuildCount_; }
    [[nodiscard]] std::vector<RaidSpacePortalProjection>
    visibleRaidSpacePortals() const;
    [[nodiscard]] bool raidSpacePortalInteractionInRange() const noexcept;
    [[nodiscard]] bool spaceTransitionedLastUpdate() const noexcept;
    [[nodiscard]] std::optional<RaidInteriorMapProjection>
    activeInteriorMapProjection() const noexcept;
    [[nodiscard]] std::optional<ContentRect> freightBreachDoorBounds() const noexcept;
    [[nodiscard]] std::optional<ContentRect> nearbyFreightBreachDoor() const;
    [[nodiscard]] bool tryOpenFreightBreachDoor(const std::function<bool()> &commitWedge);
    [[nodiscard]] std::optional<RaidRoadBlockadeView> roadBlockade() const;
    [[nodiscard]] bool tryOpenRoadBlockade(const std::function<bool()> &commitWedge);
    void confirmRaidInteriorLayout(const RaidSpaceDefinitionId &spaceId) noexcept;
    [[nodiscard]] const RaidOutdoorPresentationProjection &
    outdoorPresentation(ContentRect visibleWorldBounds) const;

    [[nodiscard]]
    const std::vector<Particle> &
    particles() const;

    [[nodiscard]] const std::vector<HitResult> &
    hitResultsLastUpdate() const noexcept;
    [[nodiscard]] HitFeedbackPresentationSnapshot hitFeedbackPresentation() const noexcept
    { return shooting_.hitFeedbackPresentation(); }
    [[nodiscard]] HitFeedbackPresentationSnapshot observableHitFeedback() const;

    [[nodiscard]]
    const std::vector<GroundItem> &
    groundItems() const noexcept;

    // UI 编排层通过该入口执行受控的 Inventory 操作。
    // 外部仍不能直接访问 GridInventory 的内部容器。
    [[nodiscard]]
    GridInventory &
    inventory() noexcept;

    [[nodiscard]]
    const GridInventory &
    inventory() const noexcept;

    [[nodiscard]]
    GridInventory &
    containerInventory() noexcept;

    [[nodiscard]]
    const GridInventory &
    containerInventory() const noexcept;

    [[nodiscard]]
    const StorageCabinet &storageCabinet() const noexcept;

    [[nodiscard]]
    const ExtractionPoint &extractionPoint() const noexcept;

    [[nodiscard]] const std::optional<ExtractionPoint> &
    emergencyExtractionPoint() const noexcept;

    [[nodiscard]] bool canCallSignalExtraction(const GameplayInput &input) const noexcept;
    [[nodiscard]] bool signalInterruptedVisible() const noexcept
    { return signalInterruptedSeconds_ > 0.0F; }

    [[nodiscard]] const std::optional<ExtractionPoint> &
    conditionalExtractionPoint() const noexcept;

    [[nodiscard]] std::uint64_t
    conditionalExtractionMaximumWeightGrams() const noexcept;

    [[nodiscard]] const std::optional<ExtractionPoint> &
    credentialExtractionPoint() const noexcept;
    [[nodiscard]] bool credentialExtractionUnlocked() const noexcept;
    [[nodiscard]] const std::optional<RaidCredentialExtractionSnapshot> &
    credentialExtractionSnapshot() const noexcept;
    [[nodiscard]] bool credentialInvestigationInRange() const;
    [[nodiscard]] float credentialInvestigationProgress() const noexcept;
    [[nodiscard]] std::optional<RaidCredentialInvestigationFact>
    pendingCredentialInvestigation() const;
    void acknowledgeCredentialInvestigation() noexcept;
    void cancelCredentialInvestigationInteraction() noexcept;
    // Pause can stop simulation updates entirely. Its service hook interrupts
    // only the new credential interactions; existing routes retain their rules.
    void interruptCredentialInteraction() noexcept;
    void interruptFuelExtraction() noexcept;
    [[nodiscard]] std::optional<AssetInstanceId> completedFuelExtractionAsset() const noexcept;

    [[nodiscard]]
    const RaidSession &raidSession() const noexcept;
    [[nodiscard]] bool triggerHighRiskForDeveloper() noexcept;
    [[nodiscard]] const RaidTacticalMapState &
    tacticalMap() const noexcept;
    [[nodiscard]] std::optional<Vec2> visibleFieldIntelligenceContact() const noexcept;
    [[nodiscard]] RaidFieldTradeAccess fieldIntelligenceTradeStatus() const noexcept;
    void grantFieldIntelligence(RaidIntelligenceCategory category) noexcept;

    [[nodiscard]] std::size_t aliveEnemyCount() const noexcept;
    [[nodiscard]] std::size_t aliveInitialEnemyCount() const noexcept;
    [[nodiscard]] std::uint32_t highRiskPressureWaveCount() const noexcept;
    [[nodiscard]] std::uint32_t highRiskActiveEnemyCap() const noexcept;
    [[nodiscard]] float highRiskNextWaveSeconds() const noexcept;

    [[nodiscard]] const std::optional<ContentRect> &
    highRiskControlPoint() const noexcept;
    [[nodiscard]] const std::optional<ContentRect> &
    highRiskAdvancedResourceArea() const noexcept;
    [[nodiscard]] float highRiskControlProgress() const noexcept;
    [[nodiscard]] float highRiskControlTimeRemaining() const noexcept;
    [[nodiscard]] bool highRiskControlInteractionInRange() const noexcept;

    [[nodiscard]] const std::optional<ContentRect> &
    ordinarySurvivorRescuePoint() const noexcept;
    [[nodiscard]] float ordinarySurvivorRescueProgress() const noexcept;
    [[nodiscard]] float ordinarySurvivorRescueTimeRemaining() const noexcept;
    [[nodiscard]] bool ordinarySurvivorRescueInteractionInRange() const noexcept;
    [[nodiscard]] bool ordinarySurvivorRescueReady() const noexcept;
    [[nodiscard]] std::optional<HospitalEscortView> hospitalEscort() const;
    void confirmOrdinarySurvivorRescue() noexcept;
    void cancelOrdinarySurvivorRescueInteraction() noexcept;

    [[nodiscard]] std::optional<RaidNightRendezvousView> nightRendezvousView() const noexcept;
    [[nodiscard]] bool nightRendezvousUnavailable() const noexcept { return nightRendezvousUnavailable_; }
    [[nodiscard]] bool nightRendezvousReady() const noexcept;
    [[nodiscard]] bool takeNightRendezvousCommitRequest() noexcept;
    [[nodiscard]] bool nightRendezvousConsumedInteraction() const noexcept
    { return nightRendezvousConsumedInteraction_; }
    [[nodiscard]] bool nightRendezvousSignaledLastUpdate() const noexcept
    { return nightRendezvousSignaledLastUpdate_; }
    void confirmNightRendezvous() noexcept;

    [[nodiscard]] float weaponSpreadDegrees() const noexcept;
    [[nodiscard]] WeaponAccuracyProjection
    weaponAccuracyProjection() const noexcept;
    [[nodiscard]] float weaponVisualRecoilPixels() const noexcept;
    [[nodiscard]] Vec2 weaponAimWorldPosition() const noexcept;
    [[nodiscard]] Vec2 weaponAimDirection() const noexcept;
    [[nodiscard]] float weaponAimDownSightsProgress() const noexcept;
    [[nodiscard]] WeaponOpticMode configuredWeaponOptic() const noexcept { return shooting_.configuredWeaponOptic(); }
    [[nodiscard]] WeaponOpticMode activeWeaponOptic() const noexcept
    {
        const auto optic = shooting_.activeWeaponOptic();
        return optic != WeaponOpticMode::None && (!raidSession_.isActive() || player_.isControlled())
            ? WeaponOpticMode::None : optic;
    }
    [[nodiscard]] const std::optional<WeaponOpticViewProjection> &weaponOpticView() const noexcept
    { return shooting_.weaponOpticView(); }
    [[nodiscard]] bool willOpenHighOptic(const GameplayInput &input, float deltaTime) const noexcept
    { return raidSession_.isActive() && !player_.isControlled() && shooting_.willOpenHighOptic(input, deltaTime); }
    void cancelWeaponOptic() noexcept { shooting_.cancelWeaponOptic(); }
    void clearWeaponOptic() noexcept { shooting_.clearWeaponOptic(); }
    [[nodiscard]] bool weaponAimBeyondEffectiveRange() const noexcept;
    [[nodiscard]] bool weaponAimBeyondMaximumRange() const noexcept;
    [[nodiscard]] bool shotFiredLastUpdate() const noexcept;
    [[nodiscard]] WeaponAcousticsDefinition shotAcousticsLastUpdate() const noexcept;
    [[nodiscard]] std::size_t enemiesAlertedLastUpdate() const noexcept;
    [[nodiscard]] std::size_t navigationQueriesLastUpdate() const noexcept;
    [[nodiscard]] const RaidSimulationWorkload &
    simulationWorkloadLastUpdate() const noexcept;
    [[nodiscard]] std::vector<std::uint64_t>
    navigationRefreshCounts() const;
    void configureWeaponFire(const WeaponUseDefinition &definition);
    void configureWeaponFire(
        const WeaponUseDefinition &definition,
        const WeaponHandlingParameters &handling,
        bool preserveWeaponFireTransientState);
    void configureWeaponAmmunition(int penetration,
                                  int unarmoredDamageBonus = 0) noexcept;
    void configureMelee(std::optional<MeleeUseDefinition> use, bool usable, std::uint64_t assetId=0) noexcept {melee_.configure(use,usable,assetId);}
    [[nodiscard]] const WorldMeleeRuntime &melee() const noexcept {return melee_;}
    [[nodiscard]] bool commitPreparedMelee();
    void cancelPreparedMelee() noexcept {melee_.cancelPrepared();}
    void cancelHeavyMelee() noexcept {melee_.cancelHeavyCharge();}
    [[nodiscard]] bool isAlphaRaidWorld() const noexcept;
    [[nodiscard]] bool restorePlayerHealth(int amount);

    [[nodiscard]]
    bool markPlayerDead() noexcept;

    // 只有活动 Raid 接受伤害。致死时在同一命令内把 RaidSession
    // 转为 PlayerDead，避免出现 HP 为 0 但 Raid 仍活动的状态。
    [[nodiscard]]
    bool damagePlayer(int damage);

    [[nodiscard]] std::vector<PlayerDamageObservation>
    takePlayerDamageObservations();

    void emitPlayerNoise(float radius) noexcept;
    [[nodiscard]] std::optional<FireSupportView> squadMember(RaidSquadRole) const;
    [[nodiscard]] bool setSquadOrders(RaidSquadRole, FireSupportMovementOrder, FireSupportFireOrder) noexcept;
    [[nodiscard]] bool setAllSquadOrders(FireSupportMovementOrder, FireSupportFireOrder) noexcept;
    [[nodiscard]] bool authorizeSniperShot(std::uint64_t token) noexcept;
    void cancelSniperAuthorization() noexcept;
    [[nodiscard]] std::optional<FireSupportShotEvent> squadShotLastUpdate(RaidSquadRole) const noexcept;
    [[nodiscard]] const RaidSpaceDefinitionId &squadSpaceId(RaidSquadRole) const noexcept;
    [[nodiscard]] std::optional<int> squadHealth(RaidSquadRole) const noexcept;
    [[nodiscard]] std::optional<Vec2> squadDeathPosition(RaidSquadRole) const noexcept;
    [[nodiscard]] bool canInteractWithSquad(RaidSquadRole) const noexcept;
    [[nodiscard]] bool medicCanReach(std::optional<RaidSquadRole> patient) const noexcept;
    [[nodiscard]] bool squadMedicalThreat() const noexcept;
    void setMedicWorking(bool working) noexcept { medicWorking_ = working; }
    void restoreSquadHealth(RaidSquadRole, int);
    bool setFireSupportOrders(FireSupportMovementOrder, FireSupportFireOrder) noexcept;
    [[nodiscard]] std::optional<FireSupportView> fireSupport() const;
    [[nodiscard]] std::optional<int> fireSupportHealth() const noexcept;
    [[nodiscard]] bool fireSupportTreatmentBlocked() const noexcept;
    void restoreFireSupportHealth(int amount);
    // Death does not move the companion out of its actual Raid space.
    [[nodiscard]] const RaidSpaceDefinitionId &fireSupportSpaceId() const noexcept;
    [[nodiscard]] std::optional<Vec2> fireSupportDeathPosition() const noexcept;
    [[nodiscard]] bool canInteractWithFireSupport() const noexcept;
    [[nodiscard]] const std::vector<RaidArmedScavengerView> &armedScavengers() const noexcept { return scavengerViews_; }
    [[nodiscard]] bool confirmArmedScavengerEscape(std::string_view sourceId);
    [[nodiscard]] const std::vector<FireSupportShotEvent> &armedScavengerShotsLastUpdate() const noexcept { return scavengerShotEvents_; }
    [[nodiscard]] std::optional<FireSupportShotEvent> fireSupportShotLastUpdate() const noexcept;
    [[nodiscard]] bool noiseDecoyEnabled() const noexcept { return noiseDecoyEnabled_; }
    [[nodiscard]] const RaidNoiseDecoyRuntime &noiseDecoy() const noexcept { return noiseDecoy_; }
    [[nodiscard]] std::optional<NoiseDecoyLanding> previewNoiseDecoy(Vec2 aim, const NoiseDecoyUseDefinition &) const;
    void commitNoiseDecoy(RaidNoiseDecoyRuntime prepared) noexcept { noiseDecoy_ = std::move(prepared); }
    [[nodiscard]] bool grenadeEnabled() const noexcept { return grenadeEnabled_; }
    [[nodiscard]] const RaidSmokeRuntime &smoke() const noexcept { return smoke_; }
    [[nodiscard]] const RaidFireZoneRuntime &fireZones() const noexcept { return fireZones_; }
    [[nodiscard]] const RaidProximityAlarmRuntime &proximityAlarms() const noexcept { return proximityAlarms_; }
    [[nodiscard]] std::optional<RaidAlarmPlacement> previewProximityAlarm(Vec2 aim, const ProximityAlarmUseDefinition &) const;
    void commitProximityAlarms(RaidProximityAlarmRuntime prepared) noexcept { proximityAlarms_ = std::move(prepared); }
    [[nodiscard]] bool proximityAlarmTriggeredLastUpdate() const noexcept { return alarmTriggeredLastUpdate_; }
    [[nodiscard]] float proximityAlarmAlertSeconds() const noexcept { return alarmAlertSeconds_; }
    [[nodiscard]] std::optional<FireZoneLanding> previewFireZone(Vec2 aim, const FireZoneUseDefinition &) const;
    void commitFireZone(RaidFireZoneRuntime prepared) noexcept { fireZones_ = std::move(prepared); }
    [[nodiscard]] bool fireZoneReaches(const RaidFireZone &, Vec2) const noexcept;
    [[nodiscard]] std::shared_ptr<const RaidIlluminationCoverage> fireZoneCoverage(const RaidFireZone &) const;
    [[nodiscard]] bool hasPendingFireDamage() const noexcept { return !pendingFireTicks_.empty(); }
    [[nodiscard]] std::optional<RaidFireDamageTick> prepareFireDamageTick() const;
    [[nodiscard]] bool validatePreparedFireDamageTick(const RaidFireDamageTick &) const;
    bool commitPreparedFireDamageTick(const RaidFireDamageTick &);
    [[nodiscard]] std::optional<SmokeLanding> previewSmoke(Vec2 aim, const SmokeUseDefinition &) const;
    void commitSmoke(RaidSmokeRuntime prepared) noexcept { smoke_ = std::move(prepared); refreshSmokeSight(); }
    [[nodiscard]] bool smokeBlocksSight(Vec2 from, Vec2 to) const noexcept { return activeSmokeSight_.blocks(from, to); }
    [[nodiscard]] const RaidGrenadeRuntime &grenade() const noexcept { return grenade_; }
    [[nodiscard]] const ThrownIlluminationRuntime &thrownIllumination() const noexcept { return thrownIllumination_; }
    [[nodiscard]] std::optional<ThrownIlluminationLanding> previewThrownIllumination(
        Vec2, const ThrownIlluminationUseDefinition &) const;
    void commitThrownIllumination(ThrownIlluminationRuntime prepared) noexcept { thrownIllumination_ = std::move(prepared); }
    [[nodiscard]] std::optional<GrenadeLanding> previewGrenade(Vec2, const GrenadeUseDefinition &) const;
    void commitGrenade(RaidGrenadeRuntime prepared) noexcept;
    [[nodiscard]] std::span<const RaidVolatileBarrelView> volatileBarrels() const noexcept
    { return inOutdoorRaidSpace() ? std::span<const RaidVolatileBarrelView>{volatileBarrels_} : std::span<const RaidVolatileBarrelView>{}; }
    [[nodiscard]] bool hasPendingRaidExplosion() const noexcept
    { return grenade_.pendingExplosion().has_value() || (!pendingBarrelIds_.empty() && inOutdoorRaidSpace()); }
    [[nodiscard]] std::optional<GrenadeExplosionPlan> prepareGrenadeExplosion() const;
    [[nodiscard]] bool validatePreparedGrenadeExplosion(const GrenadeExplosionPlan &) const;
    [[nodiscard]] bool commitPreparedGrenadeExplosion(const GrenadeExplosionPlan &);
    [[nodiscard]] const std::optional<GrenadeExplosion> &lastGrenadeExplosion() const noexcept { return lastGrenadeExplosion_; }
    [[nodiscard]] bool grenadeExplodedLastUpdate() const noexcept { return grenadeExplodedLastUpdate_; }


    [[nodiscard]]
    bool canInteractWithContainer() const noexcept;

    [[nodiscard]]
    bool searchStorageCabinet();

    // 测试和确定性模拟通过该入口注入随机序列。
    [[nodiscard]]
    bool searchStorageCabinet(
        LootRandomSource &random);

    // 只允许把玩家背包中的物品丢到角色朝向前方。
    // 失败时玩家背包和地面物品列表均保持不变。
    [[nodiscard]]
    bool dropInventoryItem(
        ItemInstanceId instanceId);

    [[nodiscard]]
    bool dropInventoryItem(
        ItemInstanceId instanceId,
        ItemOrientation orientation);

    [[nodiscard]]
    bool dropInventoryItemQuantity(
        ItemInstanceId instanceId,
        std::uint32_t quantity,
        ItemOrientation orientation);

    [[nodiscard]]
    bool transferInventoryItemQuantity(
        bool sourceIsPlayerInventory,
        ItemInstanceId instanceId,
        std::uint32_t quantity);

    [[nodiscard]]
    bool placeInventoryItemQuantity(
        bool sourceIsPlayerInventory,
        bool destinationIsPlayerInventory,
        ItemInstanceId instanceId,
        std::uint32_t quantity,
        GridPosition destinationOrigin,
        ItemOrientation destinationOrientation);

    [[nodiscard]]
    int score() const noexcept;

    // 返回尚未分配的下一个 ID，而不是当前存活实例的最大值。
    // 已销毁物品的 ID 也不会因此被复用。
    [[nodiscard]]
    ItemInstanceId nextItemInstanceId() const noexcept;

private:
    friend struct EnemyLifecycleTestAccess;
    friend struct FreightBreachWorldTestAccess;
    friend struct RoadBlockadeWorldTestAccess;
    struct EnemyNavigationRuntime
    {
        std::optional<Vec2> goal;
        std::optional<Vec2> waypoint;
        float refreshRemainingSeconds{};
        bool targetVisible{};
        bool initialized{};
        std::uint64_t refreshCount{};
    };

    struct EnemyEncounterRuntime
    {
        std::string groupInstanceId;
        RaidEncounterKind kind{RaidEncounterKind::Guard};
        Vec2 home{};
        std::vector<Vec2> patrolPoints;
        std::size_t patrolPointIndex{};
        float ambushActivationDistance{240.0F};
    };

    struct EnemyAttachedState
    {
        EnemyNavigationRuntime navigation;
        EnemyEncounterRuntime encounter;
        bool noiseResponsive{};
        double fireExposure{};
        std::uint64_t fireExposureGeneration{};
    };

    struct InteriorRuntime
    {
        RaidSpaceDefinitionId id;
        std::string displayName;
        bool layoutKnown{};
        Vec2 worldSize{};
        ContentRect exteriorEntrance;
        Vec2 exteriorReturn{};
        Vec2 interiorSpawn{};
        ContentRect interiorExit;
        EnemyRoster<EnemyAttachedState> enemies;
        std::vector<BallisticBlocker> ballisticBlockers;
        std::optional<RaidSpaceBlockerIndex> blockerIndex;
        std::size_t navigationScheduleCursor{};
        std::size_t initialEnemyCount{};
        RaidLightEnvironment lightEnvironment{RaidLightEnvironment::WeakIndoor};
        std::uint64_t geometryRevision{1};
    };

    struct NavigationFieldCache
    {
        RaidSpaceDefinitionId spaceId;
        Vec2 actorSize{};
        RaidSpaceNavigationField field;
    };

    struct PreparedFreightBreach
    {
        std::size_t interiorIndex{};
        std::vector<BallisticBlocker> blockers;
        std::optional<RaidSpaceBlockerIndex> blockerIndex;
        std::vector<NavigationFieldCache> navigationFields;
    };

    struct RoadBlockadeRuntime
    {
        ContentRect bounds;
        RaidRoadBlockadePhase phase{RaidRoadBlockadePhase::Preparing};
        float secondsRemaining{};
        std::vector<BallisticBlocker> inactiveBlockers;
        std::optional<RaidSpaceBlockerIndex> inactiveBlockerIndex;
        std::vector<NavigationFieldCache> closedNavigationFields;
    };

    struct OutdoorPresentationChunk
    {
        std::vector<std::size_t> terrainSpanIndices;
        std::vector<std::size_t> roadCellIndices;
        std::vector<std::size_t> propIndices;
        std::vector<std::size_t> landmarkIndices;
        std::vector<std::size_t> resourcePointIndices;
        std::vector<std::size_t> districtLabelIndices;
    };

    struct PlayerHealthOverrideTag
    {
    };

    GameplayWorld(
        std::vector<GroundItemSpawn> initialGroundItems,
        InventoryGridSize inventorySize,
        ItemInstanceId firstItemInstanceId,
        std::vector<EnemySpawn> initialEnemies,
        int playerMaxHealth,
        PlayerHealthOverrideTag);

    Player player_;

    WorldShootingRuntime shooting_;
    WorldMeleeRuntime melee_;
    // Fixed role slots preserve actor identity while the active space changes.
    std::array<std::optional<RaidFireSupportRuntime>, 3> squad_;
    std::array<std::optional<std::size_t>, 3> squadInteriors_;
    bool squadPresent_{};
    bool medicWorking_{};
    struct ScavengerWeaponRuntime
    {
        FireSupportConfig pistol;
        Vec2 guardCenter;
        float elapsed{}, contactProtection{};
        std::optional<CombatTargetId> target; // Zero denotes the player.
        std::optional<LogicalBallisticFlight> flight;
        std::vector<CombatTargetId> attackLocks;
        std::optional<Vec2> retreatPoint, nextRetreatWaypoint;
        float retreatRetrySeconds{};
    };
    std::vector<RaidArmedScavengerView> scavengerViews_;
    std::vector<ScavengerWeaponRuntime> scavengerWeapons_;
    std::vector<FireSupportShotEvent> scavengerShotEvents_;
    ShotId nextScavengerShotId_{1};
    struct InfectedGuardTarget
    {
        int guard{-1};
        std::optional<bool> playerVisible;
    };
    // Outdoor blockers are immutable. Both endpoints identify the exact ray,
    // even when a retirement changes the actor occupying a roster slot.
    struct ScavengerSightSample
    {
        Vec2 start{}, end{};
        std::optional<bool> visible;
    };
    std::vector<ScavengerSightSample> scavengerSightSamples_;
    [[nodiscard]] bool scavengerSight(Vec2 start, Vec2 end, ScavengerSightSample &sample);
    [[nodiscard]] InfectedGuardTarget selectInfectedGuardTarget(
        const Enemy &, Vec2 position, Vec2 player, std::span<Enemy *const> guards,
        ScavengerSightSample *sight);
    void addArmedScavenger(const EnemySpawn &, CombatTargetId);
    void checkScavengerRetreat(std::size_t index, const Enemy &actor);
    void advanceScavengerRetreats(std::span<Enemy *const> actors, float seconds);
    void updateScavengerWeapons(float seconds);
    void clearScavengerAim() noexcept;
    template <bool CompanionsEnabled, bool ScavengersEnabled = false>
    bool advanceRaidEnemies(float deltaTime, std::span<RaidFireSupportRuntime *const> squad, RaidCompanionRuntime *escort = nullptr);
    void emitRaidNoise(Vec2 source, float radius) noexcept;
    void consumeEnemyRemovals(std::span<const EnemyRemovalFact>);
    [[nodiscard]] Vec2 lostWeaponCorpseSearchPoint(Vec2 deathCenter) const noexcept;
    CombatTargetId nextCombatTargetId_{1};
    EnemyRoster<EnemyAttachedState> enemies_;
    std::map<CombatTargetId, RaidCorpseDeathFact> corpseSources_;
    std::vector<TrainingCorpseDeathFact> trainingCorpseDeaths_;
    std::vector<RaidCorpseDeathFact> pendingCorpseDeaths_;
    CombatTargetId lostWeaponCarrierId_{kInvalidCombatTargetId};
    Vec2 lostWeaponCarrierHalfSize_{};
    std::optional<Vec2> lostWeaponPursuitFallback_;
    std::optional<Vec2> lostWeaponCarrierDeath_;
    std::size_t initialOutdoorEnemyCount_{};
    std::vector<BallisticBlocker> ballisticBlockers_;
    std::optional<Vec2> fieldIntelligenceContact_;
    RaidGeneratedMapLayout outdoorLayout_;
    std::uint32_t outdoorColumns_{};
    std::uint32_t outdoorRows_{};
    std::uint32_t outdoorChunkSizeCells_{};
    std::uint32_t outdoorChunkColumns_{};
    std::uint32_t outdoorChunkRows_{};
    std::vector<OutdoorPresentationChunk> outdoorPresentationChunks_;
    mutable RaidOutdoorPresentationProjection outdoorPresentationCache_;
    mutable RaidVisibilityProjection indoorObservationCache_;
    mutable RaidVisibilityBoundsQuery observationBoundsQuery_;
    mutable bool indoorObservationCacheReady_{};
    mutable std::uint64_t indoorObservationBuildCount_{};
    // Frame-local stable-ID lookup; verify the live ID at its position on every
    // query and clear before update/removal/spawn/roster replacement. No actor
    // pointers.
    struct EnemyObservationCacheEntry
    {
        CombatTargetId id{};
        Rect bounds;
        bool computed{}, observable{};
        std::size_t position{};
    };
    mutable std::vector<EnemyObservationCacheEntry> enemyObservationCache_;
    mutable bool enemyObservationIndexReady_{};
    mutable std::uint64_t enemyObservationSolveCount_{};
    mutable RaidObservationWork observationWork_;
    mutable std::array<RaidVisibilityProjection,2> weaponLightBackgroundCache_;
    mutable std::shared_ptr<const RaidIlluminationCoverage> thrownIlluminationCoverage_;
    float thrownIlluminationGlareSeconds_{};
    [[nodiscard]] const std::shared_ptr<const RaidIlluminationCoverage> &
        activeThrownIlluminationCoverage() const;
    [[nodiscard]] RaidIlluminationObservationKey currentThrownIlluminationObservation() const;
    void updateThrownIlluminationGlare(float seconds);
    std::uint64_t weaponLightDevice_{}, weaponLightBattery_{};
    float weaponLightAngle_{}, weaponLightDistance_{}, weaponLightBackgroundRadius_{};
    bool raidDaylight_{true};
    bool lightConeObservationEnabled_{};
    bool observationDirectionFrozen_{};
    Vec2 observationDirection_{1,0};
    RaidSpaceDefinitionId observationLightSpace_;
    RaidLightEnvironment observationLightEnvironment_{RaidLightEnvironment::DayOutdoor};
    RaidSightParameters observationParameters_{180,1000};
    RaidSightParameters observationTransitionStart_{180,1000};
    RaidSightParameters nightVisionOutdoor_, nightVisionIndoor_;
    float observationLightTransitionSeconds_{0.5F};
    void updateObservationLight(float seconds);
    [[nodiscard]] RaidLightEnvironment currentObservationEnvironment() const noexcept;
    [[nodiscard]] RaidSightParameters currentObservationParameters() const noexcept;
    [[nodiscard]] Vec2 currentObservationDirection() const noexcept;
    mutable bool outdoorPresentationCacheValid_{};
    mutable std::uint32_t outdoorPresentationFirstChunkColumn_{};
    mutable std::uint32_t outdoorPresentationLastChunkColumn_{};
    mutable std::uint32_t outdoorPresentationFirstChunkRow_{};
    mutable std::uint32_t outdoorPresentationLastChunkRow_{};
    mutable std::uint32_t outdoorPresentationVisitSequence_{};
    mutable std::uint64_t outdoorPresentationCacheRevision_{};
    mutable std::vector<std::uint32_t> outdoorTerrainVisitStamps_;
    mutable std::vector<std::uint32_t> outdoorRoadVisitStamps_;
    mutable std::vector<std::uint32_t> outdoorPropVisitStamps_;
    mutable std::vector<std::uint32_t> outdoorLandmarkVisitStamps_;
    mutable std::vector<std::uint32_t> outdoorResourcePointVisitStamps_;
    mutable std::vector<std::uint32_t> outdoorDistrictVisitStamps_;
    std::optional<RaidSpaceBlockerIndex> outdoorBlockerIndex_;
    std::size_t outdoorNavigationScheduleCursor_{};
    std::vector<InteriorRuntime> interiors_;
    std::optional<std::size_t> activeInteriorIndex_;
    std::uint64_t activeGeometryRevision_{1};
    std::uint64_t outdoorGeometryRevision_{1};
    std::optional<RoadBlockadeRuntime> roadBlockade_;
    std::optional<PreparedFreightBreach> preparedFreightBreach_;
    std::vector<NavigationFieldCache> navigationFieldCache_;
    bool spaceTransitionedLastUpdate_{};

    std::vector<GroundItem> groundItems_;
    GridInventory inventory_{{10, 6}};
    StorageCabinet storageCabinet_;
    ExtractionPoint extractionPoint_;
    std::optional<ExtractionPoint> emergencyExtractionPoint_;
    float signalInterruptedSeconds_{};
    std::optional<ExtractionPoint> conditionalExtractionPoint_;
    std::uint64_t conditionalExtractionMaximumWeightGrams_{};
    std::optional<AssetInstanceId> fuelExtractionAsset_;
    std::optional<ExtractionPoint> credentialExtractionPoint_;
    std::optional<RaidCredentialExtractionSnapshot> credentialExtraction_;
    float credentialInvestigationElapsedSeconds_{};
    std::optional<RaidCredentialInvestigationFact> pendingCredentialInvestigation_;
    RaidSession raidSession_;
    RaidTacticalMapState tacticalMap_;

    std::vector<EnemySpawn> highRiskPressureSpawns_;
    float highRiskWaveIntervalSeconds_{};
    float highRiskNextWaveSeconds_{};
    std::uint32_t highRiskWaveSize_{};
    std::uint32_t highRiskActiveEnemyCap_{};
    std::uint32_t highRiskPressureWaveCount_{};
    std::size_t nextHighRiskPressureSpawnIndex_{};
    std::optional<ContentRect> highRiskControlPoint_;
    std::optional<ContentRect> highRiskAdvancedResourceArea_;
    float highRiskActivationDurationSeconds_{};
    float highRiskActivationElapsedSeconds_{};
    std::optional<ContentRect> ordinarySurvivorRescuePoint_;
    float ordinarySurvivorRescueDurationSeconds_{};
    float ordinarySurvivorRescueElapsedSeconds_{};
    bool ordinarySurvivorRescueSecured_{};
    std::optional<RaidCompanionRuntime> hospitalEscortActor_;
    HospitalEscortState hospitalEscortOrder_{HospitalEscortState::AwaitingContact};
    bool hospitalEscortCommandHeld_{};
    [[nodiscard]] bool nearHospitalEscort() const noexcept;
    [[nodiscard]] bool hospitalEscortAtHandoff() const noexcept;
    [[nodiscard]] bool hospitalEscortThreatened() const noexcept;
    std::optional<RaidNightRendezvousView> nightRendezvous_;
    bool nightRendezvousUnavailable_{};
    bool nightRendezvousConsumedInteraction_{};
    bool nightRendezvousCommitRequested_{};
    bool nightRendezvousSignaledLastUpdate_{};

    // 0 被 ItemInstance 保留为无效 ID。
    ItemInstanceId nextItemInstanceId_{1};
    SeededLootRandomSource lootRandom_;

    int score_{0};
    Vec2 worldSize_{1280.0F, 720.0F};
    std::size_t enemiesAlertedLastUpdate_{};
    std::size_t navigationQueriesLastUpdate_{};
    RaidSimulationWorkload simulationWorkloadLastUpdate_{};
    std::vector<std::size_t> blockerQueryScratch_;
    bool alphaRaidWorld_{};
    bool developerRange_{};
    bool noiseDecoyEnabled_{};
    friend struct GrenadeTestAccess;
    friend struct RunnerInfectedTestAccess;
    bool grenadeEnabled_{};
    RaidGrenadeRuntime grenade_;
    ThrownIlluminationRuntime thrownIllumination_;
    RaidSmokeRuntime smoke_;
    RaidFireZoneRuntime fireZones_;
    RaidProximityAlarmRuntime proximityAlarms_;
    bool alarmTriggeredLastUpdate_{};
    float alarmAlertSeconds_{};
    struct FireInterval { Vec2 center; float radius{}; double from{}, until{}; int damage{}; };
    std::array<FireInterval, RaidFireZoneRuntime::kCapacity> frameFireIntervals_{};
    std::size_t frameFireIntervalCount_{};
    Vec2 firePlayerFrameStart_{}, firePlayerFrameEnd_{};
    double playerFireExposure_{};
    std::uint64_t fireExposureGeneration_{1};
    struct FirePulse
    {
        std::uint64_t sequence{};
        RaidSpaceDefinitionId space;
        int playerDamage{};
        std::vector<std::pair<CombatTargetId, int>> enemies;
    };
    std::deque<FirePulse> pendingFireTicks_;
    std::uint64_t nextFireTick_{1};
    mutable std::array<std::shared_ptr<const RaidIlluminationCoverage>, RaidFireZoneRuntime::kCapacity> fireCoverage_;
    void advanceFireZones(float seconds, Vec2 before, Vec2 after, bool transitioned);
    void collectFireDamage(Vec2 point, CombatTargetId, double &credit, double from, double until, std::size_t firstPulse);
    [[nodiscard]] bool validateAreaDamageTargets(std::span<const GrenadeEnemyDamage>) const;
    void applyAreaDamageTargets(std::span<const GrenadeEnemyDamage>);
    std::vector<RaidVolatileBarrelView> volatileBarrels_;
    std::vector<BallisticBlocker> intactBarrelTargets_;
    std::vector<std::uint32_t> pendingBarrelIds_;
    RaidSmokeSight activeSmokeSight_;
    void refreshSmokeSight() noexcept;
    std::optional<GrenadeExplosion> lastGrenadeExplosion_;
    bool grenadeExplodedLastUpdate_{};
    float grenadeFlashSeconds_{};
    struct LocalNoiseInvestigation
    {
        RaidSpaceDefinitionId space;
        Vec2 point{};
        float remaining{};
        bool fromGrenade{};
    };
    using LocalNoiseListeners = std::map<CombatTargetId, LocalNoiseInvestigation>;
    LocalNoiseListeners localNoiseInvestigations_;
    [[nodiscard]] LocalNoiseListeners prepareOutdoorLocalNoise(Vec2 origin, float radius) const;
    void publishOutdoorLocalNoise(LocalNoiseListeners listeners, Vec2 origin);
    void updateLocalNoiseInvestigations(float);
    void emitGrenadeNoise(Vec2, float);
    RaidNoiseDecoyRuntime noiseDecoy_;
    void updateNoiseDecoy(float seconds);
    [[nodiscard]] std::optional<Vec2> noiseInvestigationGoal(Enemy &, bool targetVisible, Vec2 playerPosition);

    bool deferPlayerDamageResolution_{};
    float enemyDamageProtectionRemainingSeconds_{};
    std::vector<PlayerDamageObservation> pendingPlayerDamageObservations_;

    void spawnGroundItem(
        ItemId definitionId,
        Vec2 position,
        std::uint32_t quantity);

    [[nodiscard]]
    std::optional<std::size_t>
    findPickupCandidate() const;

    [[nodiscard]]
    bool itemInstanceIdExists(
        ItemInstanceId instanceId) const noexcept;

    void tryPickupOne();

    [[nodiscard]] float worldWidth() const noexcept;
    [[nodiscard]] float worldHeight() const noexcept;
    friend struct WeaponHandlingTestAccess;
    [[nodiscard]] Vec2 activeWorldSize() const noexcept;
    [[nodiscard]] EnemyRoster<EnemyAttachedState> &activeEnemies() noexcept;
    [[nodiscard]] const EnemyRoster<EnemyAttachedState> &activeEnemies() const noexcept;
    [[nodiscard]] const std::vector<BallisticBlocker> &
    activeBallisticBlockers() const noexcept;
    [[nodiscard]] const RaidSpaceBlockerIndex &
    activeBlockerIndex() const noexcept;
    [[nodiscard]] std::size_t &activeNavigationScheduleCursor() noexcept;
    [[nodiscard]] RaidSpaceNavigationField *
    activeNavigationField(Vec2 actorSize);
    void cacheNavigationFieldsForSpace(
        const RaidSpaceDefinitionId &spaceId,
        Vec2 worldSize,
        std::span<const BallisticBlocker> blockers,
        std::span<const Enemy> enemies);
    static void cacheNavigationFieldForSpace(
        std::vector<NavigationFieldCache> &cache,
        const RaidSpaceDefinitionId &spaceId,
        Vec2 actorSize,
        Vec2 worldSize,
        std::span<const BallisticBlocker> blockers);
    void prepareFreightBreachDoor();
    void prepareRoadBlockade(const RaidRoadBlockadeSnapshot &snapshot);
    void updateRoadBlockade(float highRiskDeltaTime);
    void setRoadBlockadePhase(RaidRoadBlockadePhase phase) noexcept;
    void switchRoadBlockadeGeometry() noexcept;
    [[nodiscard]] bool roadBlockadeOccupied() const noexcept;
    [[nodiscard]] bool roadBlockadeFaceObservable(Vec2 center,
        std::optional<float> maximumReach = std::nullopt) const;
    [[nodiscard]] std::size_t outdoorAliveEnemyCount() const noexcept;
    [[nodiscard]] bool tryTransitionRaidSpace(
        const GameplayInput &input,
        const std::function<bool()> &beforeSpaceTransition);
    void clearSpatialTransientPresentation() noexcept;

    void updateHighRiskPressure(float highRiskDeltaTime);
    void updateHighRiskActivation(const GameplayInput &input,
                                  float deltaTime,
                                  Vec2 playerCenter);
    void updateOrdinarySurvivorRescue(
        const GameplayInput &input,
        float deltaTime,
        Vec2 playerCenter);
    void updateNightRendezvous(const GameplayInput &input, float deltaTime, bool controlsSuppressed);
    void updateCredentialInvestigation(
        const GameplayInput &input, float deltaTime,
        bool controlsSuppressedAtFrameStart);
    [[nodiscard]] std::size_t spawnHighRiskPressureWave();
    [[nodiscard]] bool canSpawnHighRiskEnemy(
        const EnemySpawn &spawn) const noexcept;
};
