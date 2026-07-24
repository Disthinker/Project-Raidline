#include "gameplay_world.h"

#include <algorithm>
#include <limits>
#include <utility>

#include "collision.h"
#include "hit_resolution.h"

namespace
{
    constexpr float kWorldWidth{1280.0f};
    constexpr float kWorldHeight{720.0f};

    constexpr float kProjectileSpeed{600.0f};
    constexpr float kProjectileWidth{8.0f};
    constexpr float kProjectileHeight{20.0f};

    constexpr int kDefaultEnemyMaxHealth{3};
    constexpr int kProjectileDamage{1};

    constexpr int kScorePerEnemy{100};

    constexpr InventoryGridSize kDefaultInventorySize{
        10,
        6};

    std::vector<GroundItemSpawn>
    makeDefaultGroundItemSpawns()
    {
        return {
            {
                ItemId::Cola,
                Vec2{720.0f, 380.0f},
            },
            {
                ItemId::Medkit,
                Vec2{520.0f, 420.0f},
            },
            {
                ItemId::Pistol,
                Vec2{820.0f, 300.0f},
            },
            {
                ItemId::Rifle,
                Vec2{960.0f, 520.0f},
            },
        };
    }

    Rect playerBounds(
        const Player &player)
    {
        const float size =
            player.size();

        return Rect{
            player.position(),
            Vec2{size, size}};
    }

    Vec2 playerCenter(
        const Player &player)
    {
        const float halfSize =
            player.size() / 2.0f;

        const Vec2 position =
            player.position();

        return Vec2{
            position.x + halfSize,
            position.y + halfSize};
    }

    float distanceSquared(
        Vec2 first,
        Vec2 second)
    {
        const float deltaX =
            first.x - second.x;

        const float deltaY =
            first.y - second.y;

        return deltaX * deltaX +
               deltaY * deltaY;
    }
}

GameplayWorld::GameplayWorld()
    : GameplayWorld{
          kDefaultEnemyMaxHealth,
          makeDefaultGroundItemSpawns(),
          kDefaultInventorySize}
{
}

GameplayWorld::GameplayWorld(
    int enemyMaxHealth)
    : GameplayWorld{
          enemyMaxHealth,
          makeDefaultGroundItemSpawns(),
          kDefaultInventorySize}
{
}

GameplayWorld::GameplayWorld(
    int enemyMaxHealth,
    std::vector<GroundItemSpawn> initialGroundItems)
    : GameplayWorld{
          enemyMaxHealth,
          std::move(initialGroundItems),
          kDefaultInventorySize}
{
}

GameplayWorld::GameplayWorld(
    int enemyMaxHealth,
    std::vector<GroundItemSpawn> initialGroundItems,
    InventoryGridSize inventorySize)
    : inventory_{inventorySize},
      particleSystem_{
          0xC0FFEEu,
          ParticleBurstConfig{}}
{
    enemies_.emplace_back(
        Vec2{600.0f, 100.0f},
        Vec2{50.0f, 50.0f},
        Vec2{150.0f, 0.0f},
        enemyMaxHealth);

    groundItems_.reserve(
        initialGroundItems.size());

    for (
        const GroundItemSpawn &spawn :
        initialGroundItems)
    {
        spawnGroundItem(
            spawn.definitionId,
            spawn.position);
    }
}

void GameplayWorld::spawnGroundItem(
    ItemId definitionId,
    Vec2 position)
{
    // 只有 GroundItem 成功创建后才递增 ID。
    // 若 definitionId 非法，ItemInstance 构造会抛出，
    // 当前 ID 不会被跳过。
    const ItemInstanceId instanceId =
        nextItemInstanceId_;

    groundItems_.emplace_back(
        ItemInstance{
            instanceId,
            definitionId},
        position);

    ++nextItemInstanceId_;
}

std::optional<std::size_t>
GameplayWorld::findPickupCandidate() const
{
    const Rect playerPickupBounds =
        playerBounds(player_);

    const Vec2 center =
        playerCenter(player_);

    std::optional<std::size_t> bestIndex;
    float bestDistanceSquared =
        std::numeric_limits<float>::max();

    for (
        std::size_t index = 0;
        index < groundItems_.size();
        ++index)
    {
        const GroundItem &groundItem =
            groundItems_[index];

        // 正常情况下，失效物品会立即从 groundItems_
        // 删除。这里仍采用 fail-safe 跳过。
        if (!groundItem.item().valid())
        {
            continue;
        }

        if (!isCollision(
                playerPickupBounds,
                groundItem.pickupBounds()))
        {
            continue;
        }

        const float candidateDistanceSquared =
            distanceSquared(
                center,
                groundItem.position());

        // 只在严格更近时替换。
        // 距离相同不会替换，因此保留较早的 vector 下标。
        if (
            !bestIndex.has_value() ||
            candidateDistanceSquared <
                bestDistanceSquared)
        {
            bestIndex = index;
            bestDistanceSquared =
                candidateDistanceSquared;
        }
    }

    return bestIndex;
}

void GameplayWorld::tryPickupOne()
{
    const std::optional<std::size_t> candidate =
        findPickupCandidate();

    if (!candidate.has_value())
    {
        return;
    }

    const std::size_t index =
        *candidate;

    GroundItem &groundItem =
        groundItems_[index];

    const ItemInstance &candidateItem =
        groundItem.item();

    // 必须先确认背包有合法位置。
    //
    // 背包满时不会调用任何移动操作，因此：
    // - GroundItem 保留；
    // - ItemInstance ID 保留；
    // - 背包保持不变。
    const std::optional<GridPosition> placement =
        inventory_.findFirstFit(
            candidateItem.definitionId());

    if (!placement.has_value())
    {
        return;
    }

    // itemForTransfer() 返回原 ItemInstance 引用。
    // std::move 只把它转换为右值引用。
    //
    // tryPlace 失败时不会真正移动，因此 GroundItem
    // 仍持有有效 ItemInstance。
    const bool placed =
        inventory_.tryPlace(
            std::move(
                groundItem.itemForTransfer()),
            *placement);

    if (!placed)
    {
        // 理论上 findFirstFit 成功后这里应当成功。
        // 仍采用 fail-safe：不删除 GroundItem。
        return;
    }

    // 只有所有权已经成功进入 Inventory 后，
    // 才删除 moved-from GroundItem。
    const auto erasePosition =
        groundItems_.begin() +
        static_cast<
            std::vector<GroundItem>::difference_type>(
            index);

    groundItems_.erase(
        erasePosition);
}

void GameplayWorld::update(
    const GameplayInput &input,
    float deltaTime)
{
    // 先更新上一帧已经存在的粒子。
    particleSystem_.update(
        deltaTime);

    player_.update(
        input,
        deltaTime,
        kWorldWidth,
        kWorldHeight);

    // 使用移动完成后的 Player 位置判断拾取。
    // 每个 interactJustPressed 最多处理一件物品。
    if (input.interactJustPressed)
    {
        tryPickupOne();
    }

    for (Enemy &enemy : enemies_)
    {
        enemy.update(
            deltaTime,
            kWorldWidth);
    }

    cooldownRemaining_ -=
        deltaTime;

    if (
        input.firePressed &&
        cooldownRemaining_ <= 0.0f)
    {
        const float projectileX =
            player_.position().x +
            player_.size() / 2.0f -
            kProjectileWidth / 2.0f;

        const float projectileY =
            player_.position().y -
            kProjectileHeight;

        const Vec2 projectileVelocity{
            player_.facingDirection().x *
                kProjectileSpeed,
            player_.facingDirection().y *
                kProjectileSpeed};

        projectiles_.emplace_back(
            Vec2{
                projectileX,
                projectileY},
            projectileVelocity,
            kProjectileWidth,
            kProjectileHeight,
            kProjectileDamage);

        cooldownRemaining_ =
            fireCooldown_;
    }

    for (
        Projectile &projectile :
        projectiles_)
    {
        projectile.update(
            deltaTime);
    }

    const HitResolutionResult hitResult =
        resolveProjectileEnemyHits(
            projectiles_,
            enemies_);

    // 每次有效命中都生成粒子，
    // 与本次命中是否致命无关。
    for (
        const Vec2 &position :
        hitResult.hitPositions)
    {
        particleSystem_.emitImpact(
            position);
    }

    // enemiesKilled 是 std::size_t。
    // Score 使用 int，因此在职责边界上显式转换。
    const int killedEnemyCount =
        static_cast<int>(
            hitResult.enemiesKilled);

    score_ +=
        killedEnemyCount *
        kScorePerEnemy;

    projectiles_.erase(
        std::remove_if(
            projectiles_.begin(),
            projectiles_.end(),
            [](const Projectile &projectile)
            {
                return projectile.isOutside(
                    kWorldWidth,
                    kWorldHeight);
            }),
        projectiles_.end());
}

const Player &
GameplayWorld::player() const
{
    return player_;
}

const std::vector<Projectile> &
GameplayWorld::projectiles() const
{
    return projectiles_;
}

const std::vector<Enemy> &
GameplayWorld::enemies() const
{
    return enemies_;
}

const std::vector<Particle> &
GameplayWorld::particles() const
{
    return particleSystem_.particles();
}

const std::vector<GroundItem> &
GameplayWorld::groundItems() const noexcept
{
    return groundItems_;
}

GridInventory &
GameplayWorld::inventory() noexcept
{
    return inventory_;
}

const GridInventory &
GameplayWorld::inventory() const noexcept
{
    return inventory_;
}

int GameplayWorld::score() const noexcept
{
    return score_;
}