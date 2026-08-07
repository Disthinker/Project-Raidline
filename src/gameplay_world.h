#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include "enemy.h"
#include "gameplay_input.h"
#include "grid_inventory.h"
#include "ground_item.h"
#include "loot_table.h"
#include "particle_system.h"
#include "player.h"
#include "projectile.h"
#include "storage_cabinet.h"

// 用于配置 GameplayWorld 初始地面物品。
// GameplayWorld 根据这些定义自行生成稳定 instanceId。
struct GroundItemSpawn
{
    ItemId definitionId{};
    Vec2 position{};
    std::uint32_t quantity{1};
};

class GameplayWorld
{
public:
    GameplayWorld();

    // 为 GameplayWorldTest 提供最小 Enemy HP 配置入口。
    // 正常游戏仍使用默认的 3 HP Enemy。
    explicit GameplayWorld(int enemyMaxHealth);

    // 使用默认 10×6 背包。
    GameplayWorld(
        int enemyMaxHealth,
        std::vector<GroundItemSpawn> initialGroundItems);

    // 允许测试使用较小背包快速验证容量边界。
    GameplayWorld(
        int enemyMaxHealth,
        std::vector<GroundItemSpawn> initialGroundItems,
        InventoryGridSize inventorySize);

    void update(
        const GameplayInput &input,
        float deltaTime);

    [[nodiscard]]
    const Player &player() const;

    [[nodiscard]]
    const std::vector<Projectile> &
    projectiles() const;

    [[nodiscard]]
    const std::vector<Enemy> &
    enemies() const;

    [[nodiscard]]
    const std::vector<Particle> &
    particles() const;

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

private:
    Player player_{640.0f, 360.0f};

    std::vector<Projectile> projectiles_;
    std::vector<Enemy> enemies_;

    std::vector<GroundItem> groundItems_;
    GridInventory inventory_{{10, 6}};
    StorageCabinet storageCabinet_{
        {960.0F, 296.0F},
        {96.0F, 128.0F},
        64.0F,
        {6, 6}};

    // 0 被 ItemInstance 保留为无效 ID。
    ItemInstanceId nextItemInstanceId_{1};
    SeededLootRandomSource lootRandom_;

    float fireCooldown_{0.25f};
    float cooldownRemaining_{0.0f};

    ParticleSystem particleSystem_;
    int score_{0};

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
};
