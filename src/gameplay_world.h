#pragma once

#include <cstddef>
#include <cstdint>
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
#include "particle_system.h"
#include "player.h"
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

struct EnemySpawn
{
    Vec2 position{};
    Vec2 size{50.0F, 50.0F};
    int maxHealth{3};
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
};

struct RaidInteriorMapProjection
{
    RaidSpaceDefinitionId id;
    std::string_view displayName;
    Vec2 worldSize{};
    ContentRect exit;
    std::span<const BallisticBlocker> blockers;
};

struct RaidSpacePortalProjection
{
    RaidSpaceDefinitionId id;
    std::string_view displayName;
    ContentRect bounds;
    bool returnsOutside{};
    bool interactionInRange{};
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
    };
    std::optional<OrdinarySurvivorRescue> rescue;
    RaidIntelligenceLoadout intelligence;
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
    std::vector<RaidOutdoorLabelProjection> labels;
    std::size_t queriedChunkCount{};
    std::uint64_t cacheRevision{};
};

struct PlayerDamageObservation
{
    int baseDamage{};
    HitRegion region{HitRegion::Torso};
    int penetration{};
    int armorDamage{};
    bool weakPoint{};
    WoundSource woundSource{WoundSource::None};
};

struct WeaponAccuracyProjection
{
    Vec2 center{};
    float aimDistance{};
    float currentSpreadDegrees{};
    float minimumSpreadDegrees{};
    float maximumSpreadDegrees{};
    float worldRadius{};
    float reticleRadius{};
    bool beyondEffectiveRange{};
};

class GameplayWorld
{
public:
    GameplayWorld();

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

    void update(
        const GameplayInput &input,
        float deltaTime);

    [[nodiscard]]
    const Player &player() const;

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

    [[nodiscard]] const RaidSpaceDefinitionId &
    activeRaidSpaceId() const noexcept;
    [[nodiscard]] bool inOutdoorRaidSpace() const noexcept;
    [[nodiscard]] std::string_view
    activeRaidSpaceDisplayName() const noexcept;
    [[nodiscard]] Vec2 raidSpaceWorldSize() const noexcept;
    [[nodiscard]] std::vector<RaidSpacePortalProjection>
    visibleRaidSpacePortals() const;
    [[nodiscard]] bool raidSpacePortalInteractionInRange() const noexcept;
    [[nodiscard]] bool spaceTransitionedLastUpdate() const noexcept;
    [[nodiscard]] std::optional<RaidInteriorMapProjection>
    activeInteriorMapProjection() const noexcept;
    [[nodiscard]] const RaidOutdoorPresentationProjection &
    outdoorPresentation(ContentRect visibleWorldBounds) const;

    [[nodiscard]]
    const std::vector<Particle> &
    particles() const;

    [[nodiscard]] const std::vector<HitResult> &
    hitResultsLastUpdate() const noexcept;

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

    [[nodiscard]] const std::optional<ExtractionPoint> &
    conditionalExtractionPoint() const noexcept;

    [[nodiscard]] std::uint64_t
    conditionalExtractionMaximumWeightGrams() const noexcept;

    [[nodiscard]]
    const RaidSession &raidSession() const noexcept;
    [[nodiscard]] const RaidTacticalMapState &
    tacticalMap() const noexcept;

    [[nodiscard]] std::size_t aliveEnemyCount() const noexcept;
    [[nodiscard]] std::size_t aliveInitialEnemyCount() const noexcept;
    [[nodiscard]] std::uint32_t highRiskPressureWaveCount() const noexcept;
    [[nodiscard]] std::uint32_t highRiskActiveEnemyCap() const noexcept;

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
    void confirmOrdinarySurvivorRescue() noexcept;
    void cancelOrdinarySurvivorRescueInteraction() noexcept;

    [[nodiscard]] float weaponSpreadDegrees() const noexcept;
    [[nodiscard]] WeaponAccuracyProjection
    weaponAccuracyProjection() const noexcept;
    [[nodiscard]] float weaponVisualRecoilPixels() const noexcept;
    [[nodiscard]] Vec2 weaponAimWorldPosition() const noexcept;
    [[nodiscard]] Vec2 weaponAimDirection() const noexcept;
    [[nodiscard]] float weaponAimDownSightsProgress() const noexcept;
    [[nodiscard]] bool weaponAimBeyondEffectiveRange() const noexcept;
    [[nodiscard]] bool weaponAimBeyondMaximumRange() const noexcept;
    [[nodiscard]] bool shotFiredLastUpdate() const noexcept;
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
    struct EnemyNavigationRuntime
    {
        std::optional<Vec2> goal;
        std::optional<Vec2> waypoint;
        float refreshRemainingSeconds{};
        bool targetVisible{};
        bool initialized{};
        std::uint64_t refreshCount{};
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
        std::vector<Enemy> enemies;
        std::vector<BallisticBlocker> ballisticBlockers;
        std::vector<EnemyNavigationRuntime> enemyNavigation;
        std::optional<RaidSpaceBlockerIndex> blockerIndex;
        std::size_t navigationScheduleCursor{};
        std::size_t initialEnemyCount{};
    };

    struct TracerPresentationSegment
    {
        ShotId shotId{kInvalidShotId};
        Vec2 start{};
        Vec2 end{};
        Vec2 direction{};
        TracerStyle style{TracerStyle::Weak};
        float opacity{};
        float lifetimeSeconds{};
        float remainingSeconds{};
        float ageSeconds{};
    };

    struct NavigationFieldCache
    {
        RaidSpaceDefinitionId spaceId;
        Vec2 actorSize{};
        RaidSpaceNavigationField field;
    };

    struct OutdoorPresentationChunk
    {
        std::vector<std::size_t> terrainSpanIndices;
        std::vector<std::size_t> roadCellIndices;
        std::vector<std::size_t> propIndices;
        std::vector<std::size_t> landmarkIndices;
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

    std::vector<LogicalBallisticFlight> logicalBallistics_;
    std::vector<TracerPresentationSegment> tracerPresentations_;
    ShotFeedbackPresentationState shotFeedbackPresentation_;
    ShotId nextShotId_{1};
    CombatTargetId nextCombatTargetId_{1};
    std::vector<Enemy> enemies_;
    std::size_t initialOutdoorEnemyCount_{};
    std::vector<EnemyNavigationRuntime> enemyNavigation_;
    std::vector<BallisticBlocker> ballisticBlockers_;
    RaidGeneratedMapLayout outdoorLayout_;
    std::uint32_t outdoorColumns_{};
    std::uint32_t outdoorRows_{};
    std::uint32_t outdoorChunkSizeCells_{};
    std::uint32_t outdoorChunkColumns_{};
    std::uint32_t outdoorChunkRows_{};
    std::vector<OutdoorPresentationChunk> outdoorPresentationChunks_;
    mutable RaidOutdoorPresentationProjection outdoorPresentationCache_;
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
    mutable std::vector<std::uint32_t> outdoorDistrictVisitStamps_;
    std::optional<RaidSpaceBlockerIndex> outdoorBlockerIndex_;
    std::size_t outdoorNavigationScheduleCursor_{};
    std::vector<InteriorRuntime> interiors_;
    std::optional<std::size_t> activeInteriorIndex_;
    std::vector<NavigationFieldCache> navigationFieldCache_;
    bool spaceTransitionedLastUpdate_{};
    EnemySquadCoordinator enemySquadCoordinator_;

    std::vector<GroundItem> groundItems_;
    GridInventory inventory_{{10, 6}};
    StorageCabinet storageCabinet_;
    ExtractionPoint extractionPoint_;
    std::optional<ExtractionPoint> emergencyExtractionPoint_;
    std::optional<ExtractionPoint> conditionalExtractionPoint_;
    std::uint64_t conditionalExtractionMaximumWeightGrams_{};
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

    // 0 被 ItemInstance 保留为无效 ID。
    ItemInstanceId nextItemInstanceId_{1};
    SeededLootRandomSource lootRandom_;

    static constexpr int kDefaultWeaponDamage{1};
    WeaponFireState weaponFire_;
    WeaponAimState weaponAim_;
    int weaponBaseDamage_{kDefaultWeaponDamage};
    float weaponMaximumRange_{2048.0F};
    float weaponLogicalBallisticSpeed_{6000.0F};
    TracerStyle weaponTracerStyle_{TracerStyle::Weak};
    float weaponTracerLength_{30.0F};
    float weaponTracerOpacity_{0.42F};
    float weaponTracerLifetimeSeconds_{0.055F};

    ParticleSystem particleSystem_;
    std::vector<HitResult> hitResultsLastUpdate_;
    int score_{0};
    Vec2 worldSize_{1280.0F, 720.0F};
    bool shotFiredLastUpdate_{};
    std::size_t enemiesAlertedLastUpdate_{};
    std::size_t navigationQueriesLastUpdate_{};
    RaidSimulationWorkload simulationWorkloadLastUpdate_{};
    std::vector<std::size_t> blockerQueryScratch_;
    bool alphaRaidWorld_{};
    bool deferPlayerDamageResolution_{};
    float enemyDamageProtectionRemainingSeconds_{};
    std::vector<PlayerDamageObservation> pendingPlayerDamageObservations_;

    static constexpr float kEnemyDamageProtectionDurationSeconds{0.25F};

    void spawnGroundItem(
        ItemId definitionId,
        Vec2 position,
        std::uint32_t quantity);

    [[nodiscard]] bool resolveEnemyAttackDamage(
        EnemyAttackType type,
        int legacyDamage);

    [[nodiscard]]
    std::optional<std::size_t>
    findPickupCandidate() const;

    [[nodiscard]]
    bool itemInstanceIdExists(
        ItemInstanceId instanceId) const noexcept;

    void tryPickupOne();

    [[nodiscard]] float worldWidth() const noexcept;
    [[nodiscard]] float worldHeight() const noexcept;
    [[nodiscard]] Vec2 activeWorldSize() const noexcept;
    [[nodiscard]] std::vector<Enemy> &activeEnemies() noexcept;
    [[nodiscard]] const std::vector<Enemy> &activeEnemies() const noexcept;
    [[nodiscard]] std::vector<EnemyNavigationRuntime> &
    activeEnemyNavigation() noexcept;
    [[nodiscard]] const std::vector<EnemyNavigationRuntime> &
    activeEnemyNavigation() const noexcept;
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
    [[nodiscard]] std::size_t outdoorAliveEnemyCount() const noexcept;
    [[nodiscard]] bool tryTransitionRaidSpace(
        const GameplayInput &input) noexcept;
    void clearSpatialTransientPresentation() noexcept;

    void updateHighRiskPressure(float highRiskDeltaTime);
    void updateHighRiskActivation(const GameplayInput &input,
                                  float deltaTime,
                                  Vec2 playerCenter);
    void updateOrdinarySurvivorRescue(
        const GameplayInput &input,
        float deltaTime,
        Vec2 playerCenter);
    [[nodiscard]] std::size_t spawnHighRiskPressureWave();
    [[nodiscard]] bool canSpawnHighRiskEnemy(
        const EnemySpawn &spawn) const noexcept;
};
