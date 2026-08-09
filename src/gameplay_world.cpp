#include "gameplay_world.h"

#include <algorithm>
#include <cmath>
#include <exception>
#include <limits>
#include <stdexcept>
#include <utility>

#include "collision.h"
#include "hit_resolution.h"
#include "inventory_transfer.h"

namespace
{
    constexpr float kWorldWidth{1280.0f};
    constexpr float kWorldHeight{720.0f};

    constexpr float kProjectileSpeed{1200.0f};
    constexpr float kProjectileWidth{8.0f};
    constexpr float kProjectileHeight{8.0f};
    constexpr float kMaximumProjectileTravelPerStep{8.0F};
    constexpr float kMaximumProjectileSubsteps{256.0F};
    constexpr float kMaximumEnemyStepTime{1.0F / 120.0F};
    constexpr float kMaximumEnemySubsteps{2048.0F};

    constexpr int kDefaultEnemyMaxHealth{3};
    constexpr int kProjectileDamage{1};

    constexpr int kScorePerEnemy{100};

    std::vector<EnemySpawn> makeDefaultEnemySpawns(
        int maximumHealth)
    {
        return std::vector<EnemySpawn>{
            EnemySpawn{
                Vec2{600.0F, 100.0F},
                Vec2{50.0F, 50.0F},
                maximumHealth},
            EnemySpawn{
                Vec2{350.0F, 500.0F},
                Vec2{50.0F, 50.0F},
                maximumHealth},
            EnemySpawn{
                Vec2{930.0F, 500.0F},
                Vec2{50.0F, 50.0F},
                maximumHealth}};
    }

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
            {
                ItemId::Ammo9mm,
                Vec2{640.0f, 440.0f},
                25,
            },
            {
                ItemId::Ammo9mm,
                Vec2{760.0f, 440.0f},
                40,
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

    Vec2 enemyCenter(
        const Enemy &enemy)
    {
        const Vec2 position = enemy.position();
        const Vec2 size = enemy.size();

        return Vec2{
            position.x + size.x / 2.0F,
            position.y + size.y / 2.0F};
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
          kDefaultInventorySize,
          ItemInstanceId{1}}
{
}

GameplayWorld::GameplayWorld(
    ItemInstanceId firstItemInstanceId)
    : GameplayWorld{
          kDefaultEnemyMaxHealth,
          makeDefaultGroundItemSpawns(),
          kDefaultInventorySize,
          firstItemInstanceId}
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
    int playerMaxHealth)
    : GameplayWorld{
          makeDefaultGroundItemSpawns(),
          kDefaultInventorySize,
          ItemInstanceId{1},
          makeDefaultEnemySpawns(enemyMaxHealth),
          playerMaxHealth,
          PlayerHealthOverrideTag{}}
{
}

GameplayWorld::GameplayWorld(
    std::vector<EnemySpawn> initialEnemies,
    int playerMaxHealth)
    : GameplayWorld{
          makeDefaultGroundItemSpawns(),
          kDefaultInventorySize,
          ItemInstanceId{1},
          std::move(initialEnemies),
          playerMaxHealth,
          PlayerHealthOverrideTag{}}
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
    : GameplayWorld{
          enemyMaxHealth,
          std::move(initialGroundItems),
          inventorySize,
          ItemInstanceId{1}}
{
}

GameplayWorld::GameplayWorld(
    int enemyMaxHealth,
    std::vector<GroundItemSpawn> initialGroundItems,
    InventoryGridSize inventorySize,
    ItemInstanceId firstItemInstanceId)
    : GameplayWorld{
          std::move(initialGroundItems),
          inventorySize,
          firstItemInstanceId,
          makeDefaultEnemySpawns(enemyMaxHealth),
          3,
          PlayerHealthOverrideTag{}}
{
}

GameplayWorld::GameplayWorld(
    std::vector<GroundItemSpawn> initialGroundItems,
    InventoryGridSize inventorySize,
    ItemInstanceId firstItemInstanceId,
    std::vector<EnemySpawn> initialEnemies,
    int playerMaxHealth,
    PlayerHealthOverrideTag)
    : player_{640.0F, 360.0F, playerMaxHealth},
      inventory_{inventorySize},
      nextItemInstanceId_{firstItemInstanceId},
      particleSystem_{
          0xC0FFEEu,
          ParticleBurstConfig{}}
{
    if (firstItemInstanceId == 0)
    {
        throw std::invalid_argument{
            "GameplayWorld first ItemInstanceId must be greater than zero"};
    }

    const ItemInstanceId maximumId =
        std::numeric_limits<ItemInstanceId>::max();

    if (initialGroundItems.size() >
        static_cast<std::size_t>(
            maximumId - firstItemInstanceId))
    {
        throw std::overflow_error{
            "GameplayWorld does not have enough ItemInstanceId values"};
    }

    enemies_.reserve(initialEnemies.size());
    for (const EnemySpawn &spawn : initialEnemies)
    {
        if (!std::isfinite(spawn.position.x) ||
            !std::isfinite(spawn.position.y) ||
            !std::isfinite(spawn.size.x) ||
            !std::isfinite(spawn.size.y) ||
            spawn.size.x <= 0.0F ||
            spawn.size.y <= 0.0F ||
            spawn.maxHealth <= 0)
        {
            throw std::invalid_argument{
                "GameplayWorld EnemySpawn contains invalid values"};
        }

        enemies_.emplace_back(
            spawn.position,
            spawn.size,
            Vec2{},
            spawn.maxHealth);
    }

    groundItems_.reserve(
        initialGroundItems.size());

    for (
        const GroundItemSpawn &spawn :
        initialGroundItems)
    {
        spawnGroundItem(
            spawn.definitionId,
            spawn.position,
            spawn.quantity);
    }

    const bool raidStarted =
        raidSession_.start();

    if (!raidStarted)
    {
        throw std::logic_error{
            "GameplayWorld failed to start its raid session"};
    }
}

void GameplayWorld::spawnGroundItem(
    ItemId definitionId,
    Vec2 position,
    std::uint32_t quantity)
{
    if (nextItemInstanceId_ ==
        std::numeric_limits<ItemInstanceId>::max())
    {
        throw std::overflow_error{
            "GameplayWorld exhausted ItemInstanceId values"};
    }

    // 只有 GroundItem 成功创建后才递增 ID。
    // 若 definitionId 非法，ItemInstance 构造会抛出，
    // 当前 ID 不会被跳过。
    const ItemInstanceId instanceId =
        nextItemInstanceId_;

    groundItems_.emplace_back(
        ItemInstance{
            instanceId,
            definitionId,
            quantity},
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

    // Stack insertion completes planning and capacity reservation before it
    // changes any destination quantity or moves the ground item. Failure keeps
    // both the GroundItem and the inventory unchanged.
    const bool placed =
        tryInsertItemFirstFit(
            inventory_,
            std::move(
                groundItem.itemForTransfer()));

    if (!placed)
    {
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
    if (!raidSession_.isActive())
    {
        return;
    }

    // 先更新上一帧已经存在的粒子。
    particleSystem_.update(
        deltaTime);

    const bool controlsSuppressedAtFrameStart =
        player_.isControlled();

    player_.update(
        input,
        deltaTime,
        kWorldWidth,
        kWorldHeight);

    if (input.aimWorldPosition.has_value())
    {
        const Vec2 center = playerCenter(player_);
        static_cast<void>(
            player_.faceDirection(
                Vec2{
                    input.aimWorldPosition->x - center.x,
                    input.aimWorldPosition->y - center.y}));
    }

    // 撤离使用移动后的 Player 逻辑中心，而不是更大的渲染精灵。
    raidSession_.update(
        deltaTime,
        extractionPoint_.contains(
            playerCenter(player_)));

    // 终局形成后，本帧不再产生拾取、射击、敌人或命中结果。
    if (!raidSession_.isActive())
    {
        return;
    }

    // Bounded substeps keep attack phase transitions and one-shot hit
    // opportunities observable even when a render frame is long.
    std::size_t enemySubsteps{1U};
    if (std::isfinite(deltaTime) &&
        deltaTime > 0.0F)
    {
        const float requestedSubsteps = std::ceil(
            deltaTime / kMaximumEnemyStepTime);
        enemySubsteps = static_cast<std::size_t>(
            std::clamp(
                requestedSubsteps,
                1.0F,
                kMaximumEnemySubsteps));
    }

    const float enemyStepTime =
        deltaTime /
        static_cast<float>(enemySubsteps);

    for (std::size_t step{0U};
         step < enemySubsteps;
         ++step)
    {
        const Vec2 playerPosition =
            playerCenter(player_);
        std::vector<EnemySquadMemberSnapshot> enemySnapshots;
        enemySnapshots.reserve(enemies_.size());
        for (const Enemy &enemy : enemies_)
        {
            enemySnapshots.push_back(
                EnemySquadMemberSnapshot{
                    enemyCenter(enemy),
                    !enemy.isDead(),
                    enemy.awarenessState(),
                    enemy.attackPhase()});
        }

        const std::vector<EnemyTacticalDirective> directives =
            enemySquadCoordinator_.decide(
                enemySnapshots,
                playerPosition);

        for (std::size_t enemyIndex{0U};
             enemyIndex < enemies_.size();
             ++enemyIndex)
        {
            Enemy &enemy = enemies_[enemyIndex];

            static_cast<void>(
                enemy.updateTowardsTarget(
                    playerPosition,
                    directives[enemyIndex],
                    enemyStepTime,
                    kWorldWidth,
                    kWorldHeight));

            if (enemy.hasGrabContactOpportunity())
            {
                const std::optional<Rect> grabHitbox =
                    enemy.attackHitbox();
                if (!grabHitbox.has_value() ||
                    !isCollision(
                        *grabHitbox,
                        playerBounds(player_)))
                {
                    continue;
                }

                // Grab itself deals no damage. Contact atomically changes the
                // owned attack state to the Bite follow-up, then consumes that
                // follow-up once before mutating Player/Raid state.
                if (!enemy.confirmGrabContact())
                {
                    std::terminate();
                }

                const std::optional<EnemyAttackConfig> biteConfig =
                    enemy.attackConfig();
                if (!biteConfig.has_value() ||
                    enemy.attackType() != EnemyAttackType::Bite ||
                    !enemy.consumeAttackHit())
                {
                    std::terminate();
                }

                if (damagePlayer(biteConfig->damage))
                {
                    return;
                }

                if (biteConfig->controlDuration > 0.0F)
                {
                    static_cast<void>(
                        player_.applyControl(
                            biteConfig->controlDuration));
                }
                continue;
            }

            if (!enemy.hasAttackHitOpportunity())
            {
                continue;
            }

            const std::optional<Rect> hitbox =
                enemy.attackHitbox();
            const std::optional<EnemyAttackConfig> attackConfig =
                enemy.attackConfig();

            if (!hitbox.has_value() ||
                !attackConfig.has_value() ||
                !isCollision(
                    *hitbox,
                    playerBounds(player_)))
            {
                continue;
            }

            // Consume before mutating Player/Raid so a large frame cannot
            // submit the same attack hit twice.
            if (!enemy.consumeAttackHit())
            {
                std::terminate();
            }

            if (damagePlayer(attackConfig->damage))
            {
                return;
            }

            if (attackConfig->controlDuration > 0.0F)
            {
                static_cast<void>(
                    player_.applyControl(
                        attackConfig->controlDuration));
            }
        }
    }

    const bool controlsSuppressed =
        controlsSuppressedAtFrameStart ||
        player_.isControlled();

    if (!controlsSuppressed &&
        input.interactJustPressed)
    {
        tryPickupOne();
    }

    const std::optional<ShotSpec> shot =
        weaponFire_.update(
            !controlsSuppressed &&
                (input.firePressed || input.fireJustPressed),
            player_.facingDirection(),
            deltaTime);

    if (shot.has_value())
    {
        const Vec2 center = playerCenter(player_);
        const float muzzleDistance =
            player_.size() / 2.0F +
            std::max(kProjectileWidth, kProjectileHeight) / 2.0F;

        const Vec2 projectileCenter{
            center.x + shot->direction.x * muzzleDistance,
            center.y + shot->direction.y * muzzleDistance};

        const Vec2 projectileVelocity{
            shot->direction.x * kProjectileSpeed,
            shot->direction.y * kProjectileSpeed};

        projectiles_.emplace_back(
            Vec2{
                projectileCenter.x - kProjectileWidth / 2.0F,
                projectileCenter.y - kProjectileHeight / 2.0F},
            projectileVelocity,
            kProjectileWidth,
            kProjectileHeight,
            kProjectileDamage);
    }

    std::size_t projectileSubsteps{1U};
    if (std::isfinite(deltaTime) && deltaTime > 0.0F)
    {
        const float requestedSubsteps = std::ceil(
            kProjectileSpeed * deltaTime /
            kMaximumProjectileTravelPerStep);
        projectileSubsteps = static_cast<std::size_t>(
            std::clamp(
                requestedSubsteps,
                1.0F,
                kMaximumProjectileSubsteps));
    }

    const float projectileStepTime =
        deltaTime /
        static_cast<float>(projectileSubsteps);
    HitResolutionResult hitResult{};

    for (
        std::size_t step{0U};
        step < projectileSubsteps && !projectiles_.empty();
        ++step)
    {
        for (Projectile &projectile : projectiles_)
        {
            projectile.update(projectileStepTime);
        }

        HitResolutionResult stepResult =
            resolveProjectileEnemyHits(
                projectiles_,
                enemies_);
        hitResult.hitPositions.insert(
            hitResult.hitPositions.end(),
            stepResult.hitPositions.begin(),
            stepResult.hitPositions.end());
        hitResult.enemiesKilled +=
            stepResult.enemiesKilled;

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

GridInventory &
GameplayWorld::containerInventory() noexcept
{
    return storageCabinet_.inventory();
}

const GridInventory &
GameplayWorld::containerInventory() const noexcept
{
    return storageCabinet_.inventory();
}

const StorageCabinet &
GameplayWorld::storageCabinet() const noexcept
{
    return storageCabinet_;
}

const ExtractionPoint &
GameplayWorld::extractionPoint() const noexcept
{
    return extractionPoint_;
}

const RaidSession &
GameplayWorld::raidSession() const noexcept
{
    return raidSession_;
}

float GameplayWorld::weaponSpreadDegrees() const noexcept
{
    return weaponFire_.spreadDegrees();
}

float GameplayWorld::weaponVisualRecoilPixels() const noexcept
{
    return weaponFire_.visualRecoilPixels();
}

bool GameplayWorld::markPlayerDead() noexcept
{
    if (!raidSession_.isActive() ||
        player_.isDead())
    {
        return false;
    }

    // 兼容既有领域命令，但不绕过 Player 唯一拥有的 Health。
    // 活动玩家的当前生命严格大于 0，因此不会触发非法伤害异常。
    return damagePlayer(
        player_.health());
}

bool GameplayWorld::damagePlayer(int damage)
{
    if (!raidSession_.isActive())
    {
        return false;
    }

    const bool killed =
        player_.takeDamage(damage);

    if (!killed)
    {
        return false;
    }

    const bool markedDead =
        raidSession_.markPlayerDead();

    if (!markedDead)
    {
        // 活动态检查与 Health 的首次致死转换之间没有其他状态写入；
        // 到达这里意味着内部不变量已被破坏。
        std::terminate();
    }

    return true;
}

bool GameplayWorld::canInteractWithContainer() const noexcept
{
    return raidSession_.isActive() &&
           !player_.isControlled() &&
           storageCabinet_.canInteract(
        playerBounds(player_));
}

bool GameplayWorld::searchStorageCabinet()
{
    return searchStorageCabinet(
        lootRandom_);
}

bool GameplayWorld::searchStorageCabinet(
    LootRandomSource &random)
{
    if (!canInteractWithContainer())
    {
        return false;
    }

    if (storageCabinet_.isSearched())
    {
        return true;
    }

    if (!storageCabinet_.inventory().placedItems().empty())
    {
        return false;
    }

    const std::vector<LootStack> loot =
        defaultStorageCabinetLootTable().roll(random);

    const ItemInstanceId maximumId =
        std::numeric_limits<ItemInstanceId>::max();

    if (loot.size() >
        static_cast<std::size_t>(
            maximumId - nextItemInstanceId_))
    {
        return false;
    }

    GridInventory generatedInventory{
        InventoryGridSize{
            storageCabinet_.inventory().width(),
            storageCabinet_.inventory().height()}};

    generatedInventory.reserveForAdditionalItems(
        loot.size());

    ItemInstanceId candidateId =
        nextItemInstanceId_;

    for (const LootStack &stack : loot)
    {
        if (itemInstanceIdExists(candidateId))
        {
            return false;
        }

        ItemInstance item{
            candidateId,
            stack.definitionId,
            stack.quantity};

        const std::optional<GridPosition> origin =
            generatedInventory.findFirstFit(
                stack.definitionId);

        if (!origin.has_value() ||
            !generatedInventory.tryPlace(
                std::move(item),
                *origin))
        {
            return false;
        }

        ++candidateId;
    }

    if (!storageCabinet_.tryCommitSearchResult(
            std::move(generatedInventory)))
    {
        return false;
    }

    nextItemInstanceId_ = candidateId;
    return true;
}

bool GameplayWorld::itemInstanceIdExists(
    ItemInstanceId instanceId) const noexcept
{
    if (instanceId == 0 ||
        inventory_.quantityOf(instanceId).has_value() ||
        storageCabinet_.inventory()
            .quantityOf(instanceId)
            .has_value())
    {
        return true;
    }

    return std::any_of(
        groundItems_.begin(),
        groundItems_.end(),
        [instanceId](const GroundItem &groundItem)
        {
            return groundItem.item().instanceId() ==
                   instanceId;
        });
}

bool GameplayWorld::dropInventoryItem(
    ItemInstanceId instanceId)
{
    const auto placedIt = std::find_if(
        inventory_.placedItems().begin(),
        inventory_.placedItems().end(),
        [instanceId](const PlacedItem &placed)
        {
            return placed.item.instanceId() ==
                   instanceId;
        });

    if (placedIt == inventory_.placedItems().end())
    {
        return false;
    }

    return dropInventoryItem(
        instanceId,
        placedIt->item.orientation());
}

bool GameplayWorld::dropInventoryItem(
    ItemInstanceId instanceId,
    ItemOrientation orientation)
{
    const auto placedIt = std::find_if(
        inventory_.placedItems().begin(),
        inventory_.placedItems().end(),
        [instanceId](const PlacedItem &placed)
        {
            return placed.item.instanceId() ==
                   instanceId;
        });

    if (placedIt == inventory_.placedItems().end())
    {
        return false;
    }

    return dropInventoryItemQuantity(
        instanceId,
        placedIt->item.quantity(),
        orientation);
}

bool GameplayWorld::dropInventoryItemQuantity(
    ItemInstanceId instanceId,
    std::uint32_t quantity,
    ItemOrientation orientation)
{
    const auto placedIt = std::find_if(
        inventory_.placedItems().begin(),
        inventory_.placedItems().end(),
        [instanceId](const PlacedItem &placed)
        {
            return placed.item.instanceId() ==
                   instanceId;
        });

    if (placedIt == inventory_.placedItems().end() ||
        quantity == 0 ||
        quantity > placedIt->item.quantity())
    {
        return false;
    }

    const ItemDefinition &definition =
        itemDefinition(
            placedIt->item.definitionId());

    if (!canUseItemOrientation(
            definition,
            orientation))
    {
        return false;
    }

    const GridPosition sourceOrigin = placedIt->origin;
    const std::uint32_t sourceQuantity =
        placedIt->item.quantity();
    const bool partial = quantity < sourceQuantity;

    const Vec2 center = playerCenter(player_);
    const float playerFeetY =
        player_.position().y + player_.size();
    const Vec2 renderSize =
        orientedSize(
            definition.worldRenderSize,
            orientation);
    const float halfWidth = renderSize.x / 2.0f;
    const float halfHeight = renderSize.y / 2.0f;

    const Vec2 dropPosition{
        std::clamp(
            center.x,
            halfWidth,
            kWorldWidth - halfWidth),
        std::clamp(
            playerFeetY,
            halfHeight,
            kWorldHeight - halfHeight),
    };

    std::optional<ItemInstance> splitItem;
    if (partial)
    {
        if (nextItemInstanceId_ ==
            std::numeric_limits<ItemInstanceId>::max())
        {
            return false;
        }

        const bool splitIdAlreadyExists =
            inventory_.quantityOf(nextItemInstanceId_).has_value() ||
            storageCabinet_.inventory()
                .quantityOf(nextItemInstanceId_)
                .has_value() ||
            std::any_of(
                groundItems_.begin(),
                groundItems_.end(),
                [this](const GroundItem &groundItem)
                {
                    return groundItem.item().instanceId() ==
                           nextItemInstanceId_;
                });

        if (splitIdAlreadyExists)
        {
            return false;
        }

        splitItem.emplace(
            nextItemInstanceId_,
            placedIt->item.definitionId(),
            quantity);

        if (!splitItem->trySetOrientation(orientation))
        {
            return false;
        }
    }

    // 唯一可能抛出的容量增长发生在修改玩家背包之前。
    groundItems_.reserve(
        groundItems_.size() + 1);

    if (partial)
    {
        if (!inventory_.trySetItemQuantity(
                instanceId,
                sourceQuantity - quantity))
        {
            std::terminate();
        }

        groundItems_.emplace_back(
            std::move(*splitItem),
            dropPosition);
        ++nextItemInstanceId_;
        return true;
    }

    std::optional<ItemInstance> removed =
        inventory_.remove(instanceId);

    if (!removed.has_value())
    {
        return false;
    }

    if (!removed->trySetOrientation(orientation))
    {
        const bool restored = inventory_.tryPlace(
            std::move(*removed),
            sourceOrigin);

        if (!restored)
        {
            std::terminate();
        }

        return false;
    }

    // GroundItem 构造和已有元素移动均为 noexcept；reserve 后不再分配。
    groundItems_.emplace_back(
        std::move(*removed),
        dropPosition);

    return true;
}

bool GameplayWorld::transferInventoryItemQuantity(
    bool sourceIsPlayerInventory,
    ItemInstanceId instanceId,
    std::uint32_t quantity)
{
    GridInventory &source = sourceIsPlayerInventory
        ? inventory_
        : storageCabinet_.inventory();
    GridInventory &destination = sourceIsPlayerInventory
        ? storageCabinet_.inventory()
        : inventory_;

    const std::optional<std::uint32_t> sourceQuantity =
        source.quantityOf(instanceId);

    if (sourceQuantity.has_value() &&
        quantity < *sourceQuantity &&
        nextItemInstanceId_ ==
            std::numeric_limits<ItemInstanceId>::max())
    {
        return false;
    }

    const QuantityTransferResult result =
        tryTransferItemQuantityFirstFit(
            source,
            destination,
            instanceId,
            quantity,
            nextItemInstanceId_);

    if (result.consumedSplitInstanceId)
    {
        ++nextItemInstanceId_;
    }

    return result.succeeded;
}

bool GameplayWorld::placeInventoryItemQuantity(
    bool sourceIsPlayerInventory,
    bool destinationIsPlayerInventory,
    ItemInstanceId instanceId,
    std::uint32_t quantity,
    GridPosition destinationOrigin,
    ItemOrientation destinationOrientation)
{
    GridInventory &source = sourceIsPlayerInventory
        ? inventory_
        : storageCabinet_.inventory();
    GridInventory &destination = destinationIsPlayerInventory
        ? inventory_
        : storageCabinet_.inventory();

    const std::optional<std::uint32_t> sourceQuantity =
        source.quantityOf(instanceId);

    if (sourceQuantity.has_value() &&
        quantity < *sourceQuantity &&
        nextItemInstanceId_ ==
            std::numeric_limits<ItemInstanceId>::max())
    {
        return false;
    }

    const QuantityTransferResult result =
        tryPlaceItemQuantityAt(
            source,
            destination,
            instanceId,
            quantity,
            destinationOrigin,
            destinationOrientation,
            nextItemInstanceId_);

    if (result.consumedSplitInstanceId)
    {
        ++nextItemInstanceId_;
    }

    return result.succeeded;
}

int GameplayWorld::score() const noexcept
{
    return score_;
}

ItemInstanceId
GameplayWorld::nextItemInstanceId() const noexcept
{
    return nextItemInstanceId_;
}
