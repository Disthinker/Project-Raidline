#pragma once

#include <cstddef>
#include <optional>
#include <vector>

#include "enemy.h"
#include "gameplay_input.h"
#include "ground_item.h"
#include "item_instance.h"
#include "particle_system.h"
#include "player.h"
#include "projectile.h"

// 用于配置 GameplayWorld 初始地面物品。
// GameplayWorld 根据这些定义自行生成稳定 instanceId。
struct GroundItemSpawn
{
    ItemId definitionId{};
    Vec2 position{};
};

class GameplayWorld
{
public:
    GameplayWorld();

    // 为 GameplayWorldTest 提供最小 Enemy HP 配置入口。
    // 正常游戏仍使用默认的 3 HP Enemy。
    explicit GameplayWorld(int enemyMaxHealth);

    // 允许测试或未来地图系统配置初始地面物品，
    // 但 instanceId 始终由 GameplayWorld 生成。
    GameplayWorld(
        int enemyMaxHealth,
        std::vector<GroundItemSpawn> initialGroundItems);

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

    [[nodiscard]]
    const std::vector<ItemInstance> &
    carriedItems() const noexcept;

    [[nodiscard]]
    int score() const noexcept;

private:
    Player player_{640.0f, 360.0f};

    std::vector<Projectile> projectiles_;
    std::vector<Enemy> enemies_;

    std::vector<GroundItem> groundItems_;
    std::vector<ItemInstance> carriedItems_;

    // 0 被 ItemInstance 保留为无效 ID。
    ItemInstanceId nextItemInstanceId_{1};

    float fireCooldown_{0.25f};
    float cooldownRemaining_{0.0f};

    ParticleSystem particleSystem_;
    int score_{0};

    void spawnGroundItem(
        ItemId definitionId,
        Vec2 position);

    [[nodiscard]]
    std::optional<std::size_t>
    findPickupCandidate() const;

    void tryPickupOne();
};