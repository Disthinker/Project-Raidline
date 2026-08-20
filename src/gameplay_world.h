#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
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
#include "shot_resolution.h"
#include "storage_cabinet.h"
#include "weapon_fire.h"
#include "weapon_aim.h"
#include "content_registry.h"
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

struct RaidWorldConfig
{
    Vec2 worldSize{1280.0F, 720.0F};
    Vec2 playerSpawn{};
    ContentRect extractionPoint;
    std::vector<EnemySpawn> initialEnemies;
    int playerMaximumHealth{100};
    int playerCurrentHealth{100};
    bool deferPlayerDamageResolution{};
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

    [[nodiscard]]
    const std::vector<Enemy> &
    enemies() const;

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

    [[nodiscard]]
    const RaidSession &raidSession() const noexcept;

    [[nodiscard]] float weaponSpreadDegrees() const noexcept;
    [[nodiscard]] float weaponVisualRecoilPixels() const noexcept;
    [[nodiscard]] Vec2 weaponAimWorldPosition() const noexcept;
    [[nodiscard]] Vec2 weaponAimDirection() const noexcept;
    [[nodiscard]] float weaponAimDownSightsProgress() const noexcept;
    [[nodiscard]] bool weaponAimBeyondMaximumRange() const noexcept;
    [[nodiscard]] bool shotFiredLastUpdate() const noexcept;
    void configureWeaponFire(const WeaponUseDefinition &definition);
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
    ShotId nextShotId_{1};
    std::vector<Enemy> enemies_;
    EnemySquadCoordinator enemySquadCoordinator_;

    std::vector<GroundItem> groundItems_;
    GridInventory inventory_{{10, 6}};
    StorageCabinet storageCabinet_;
    ExtractionPoint extractionPoint_;
    RaidSession raidSession_;

    // 0 被 ItemInstance 保留为无效 ID。
    ItemInstanceId nextItemInstanceId_{1};
    SeededLootRandomSource lootRandom_;

    static constexpr int kDefaultWeaponDamage{1};
    WeaponFireState weaponFire_;
    WeaponAimState weaponAim_;
    int weaponBaseDamage_{kDefaultWeaponDamage};

    ParticleSystem particleSystem_;
    std::vector<HitResult> hitResultsLastUpdate_;
    int score_{0};
    Vec2 worldSize_{1280.0F, 720.0F};
    bool shotFiredLastUpdate_{};
    bool alphaRaidWorld_{};
    bool deferPlayerDamageResolution_{};
    std::vector<PlayerDamageObservation> pendingPlayerDamageObservations_;

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
};
