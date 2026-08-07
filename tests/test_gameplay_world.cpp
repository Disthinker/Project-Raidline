#include <gtest/gtest.h>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <vector>
#include <utility>

#include "gameplay_input.h"
#include "gameplay_world.h"

namespace
{
    GameplayInput makeFireInput()
    {
        GameplayInput input{};
        input.fireJustPressed = true;
        input.firePressed = true;
        return input;
    }

    class SequenceLootRandomSource final : public LootRandomSource
    {
    public:
        explicit SequenceLootRandomSource(
            std::vector<std::uint32_t> values)
            : values_{std::move(values)}
        {
        }

        std::uint32_t next(
            std::uint32_t) override
        {
            if (position_ >= values_.size())
            {
                throw std::runtime_error{
                    "Unexpected additional loot random draw"};
            }

            return values_[position_++];
        }

        std::size_t drawCount() const noexcept
        {
            return position_;
        }

    private:
        std::vector<std::uint32_t> values_;
        std::size_t position_{};
    };

    void movePlayerToCabinet(
        GameplayWorld &world)
    {
        GameplayInput moveRight{};
        moveRight.moveRight = true;
        world.update(moveRight, 1.0F);
        ASSERT_TRUE(world.canInteractWithContainer());
    }
} // namespace

// 初始 Player 位置是 (640, 360)
TEST(GameplayWorldTest, InitialPlayerPosition)
{
    GameplayWorld world;
    const Vec2 position = world.player().position();

    EXPECT_FLOAT_EQ(position.x, 640.0f);
    EXPECT_FLOAT_EQ(position.y, 360.0f);
}

// 初始 Projectile 集合为空
TEST(GameplayWorldTest, InitialProjectilesEmpty)
{
    GameplayWorld world;
    EXPECT_TRUE(world.projectiles().empty());
}

// 初始 Enemy 有 1 个，position=(600,100)，size=(50,50)
TEST(GameplayWorldTest, InitialEnemiesState)
{
    GameplayWorld world;
    ASSERT_EQ(world.enemies().size(), 1u);

    const Enemy &enemy = world.enemies()[0];
    const Vec2 enemyPosition = enemy.position();
    const Vec2 enemySize = enemy.size();

    EXPECT_FLOAT_EQ(enemyPosition.x, 600.0f);
    EXPECT_FLOAT_EQ(enemyPosition.y, 100.0f);
    EXPECT_FLOAT_EQ(enemySize.x, 50.0f);
    EXPECT_FLOAT_EQ(enemySize.y, 50.0f);
    EXPECT_EQ(enemy.health(), 3);
    EXPECT_EQ(enemy.maxHealth(), 3);
    EXPECT_FALSE(enemy.isDead());
}

TEST(GameplayWorldTest, InitialScoreIsZero)
{
    const GameplayWorld world;

    EXPECT_EQ(world.score(), 0);
}

// MoveRight input 更新后，world.player().position().x 变大
TEST(GameplayWorldTest, MoveRightUpdatesPlayerPosition)
{
    GameplayWorld world;
    GameplayInput input{};
    input.moveRight = true;

    world.update(input, 1.0f);

    EXPECT_GT(world.player().position().x, 640.0f);
}

// Fire 生成 Projectile
TEST(GameplayWorldTest, FireCreatesProjectile)
{
    GameplayWorld world;
    GameplayInput input = makeFireInput();

    world.update(input, 0.0f);

    ASSERT_EQ(world.projectiles().size(), 1u);
    const Projectile &projectile = world.projectiles()[0];

    EXPECT_FLOAT_EQ(projectile.position().x, 652.0f);
    EXPECT_FLOAT_EQ(projectile.position().y, 340.0f);
    EXPECT_FLOAT_EQ(projectile.width(), 8.0f);
    EXPECT_FLOAT_EQ(projectile.height(), 20.0f);
    EXPECT_EQ(projectile.damage(), 1);
}

// 不按 Fire 不生成 Projectile
TEST(GameplayWorldTest, NoFireDoesNotCreateProjectile)
{
    GameplayWorld world;
    GameplayInput input{};

    world.update(input, 0.0f);

    EXPECT_TRUE(world.projectiles().empty());
}

// Projectile 会随 deltaTime 向上移动
TEST(GameplayWorldTest, ProjectileMovesAfterSpawn)
{
    GameplayWorld world;

    GameplayInput fire = makeFireInput();
    world.update(fire, 0.0f);

    ASSERT_EQ(world.projectiles().size(), 1u);
    const float initialY = world.projectiles()[0].position().y;

    GameplayInput noInput{};
    world.update(noInput, 0.1f);

    ASSERT_EQ(world.projectiles().size(), 1u);
    EXPECT_LT(world.projectiles()[0].position().y, initialY);
}

// Projectile 命中 3 HP Enemy 后，Projectile 被消耗，
// Enemy 扣除 1 HP 但仍然保留。
TEST(
    GameplayWorldTest,
    ProjectileCanDamageMovingEnemyWithoutKillingIt)
{
    GameplayWorld world;

    GameplayInput fire = makeFireInput();
    world.update(fire, 0.0f);

    ASSERT_EQ(world.projectiles().size(), 1u);
    ASSERT_EQ(world.enemies().size(), 1u);
    EXPECT_EQ(world.enemies()[0].health(), 3);

    GameplayInput noInput{};
    world.update(noInput, 0.35f);

    EXPECT_TRUE(world.projectiles().empty());

    ASSERT_EQ(world.enemies().size(), 1u);
    EXPECT_EQ(world.enemies()[0].health(), 2);
    EXPECT_FALSE(world.enemies()[0].isDead());
    EXPECT_EQ(world.score(), 0);
}

// GameplayWorld 持有的 Enemy 不再是静态实体
TEST(GameplayWorldTest, EnemyMovesAfterWorldUpdate)
{
    GameplayWorld world;
    GameplayInput input{};

    ASSERT_EQ(world.enemies().size(), 1u);
    const Vec2 initialPosition = world.enemies()[0].position();

    world.update(input, 1.0f);

    ASSERT_EQ(world.enemies().size(), 1u);
    const Vec2 updatedPosition = world.enemies()[0].position();

    EXPECT_GT(updatedPosition.x, initialPosition.x);
    EXPECT_FLOAT_EQ(updatedPosition.y, initialPosition.y);
}

// World 中的 Enemy 会在右边界反弹
TEST(GameplayWorldTest, EnemyBouncesAtRightBoundary)
{
    GameplayWorld world;
    GameplayInput input{};

    world.update(input, 10.0f);

    ASSERT_EQ(world.enemies().size(), 1u);

    const Enemy &enemy = world.enemies()[0];

    EXPECT_FLOAT_EQ(enemy.position().x, 1230.0f);
    EXPECT_LT(enemy.velocity().x, 0.0f);
}

// 右朝向射击
TEST(GameplayWorldTest, FireAfterFacingRightMovesProjectileRight)
{
    GameplayWorld world;
    GameplayInput input = makeFireInput();

    input.moveRight = true;
    world.update(input, 0.0f);

    ASSERT_EQ(world.projectiles().size(), 1u);
    const Vec2 initialPosition = world.projectiles()[0].position();

    GameplayInput noInput{};
    world.update(noInput, 0.1f);

    ASSERT_EQ(world.projectiles().size(), 1u);
    const Vec2 finalPosition = world.projectiles()[0].position();

    EXPECT_GT(finalPosition.x, initialPosition.x);
    EXPECT_FLOAT_EQ(finalPosition.y, initialPosition.y);
}

// 左朝向射击
TEST(GameplayWorldTest, FireAfterFacingLeftMovesProjectileLeft)
{
    GameplayWorld world;
    GameplayInput input = makeFireInput();

    input.moveLeft = true;
    world.update(input, 0.0f);

    ASSERT_EQ(world.projectiles().size(), 1u);
    const Vec2 initialPosition = world.projectiles()[0].position();

    GameplayInput noInput{};
    world.update(noInput, 0.1f);

    ASSERT_EQ(world.projectiles().size(), 1u);
    const Vec2 finalPosition = world.projectiles()[0].position();

    EXPECT_FLOAT_EQ(finalPosition.y, initialPosition.y);
    EXPECT_LT(finalPosition.x, initialPosition.x);
}

// 下朝向射击
TEST(GameplayWorldTest, FireAfterFacingDownMovesProjectileDown)
{
    GameplayWorld world;
    GameplayInput input = makeFireInput();

    input.moveDown = true;
    world.update(input, 0.0f);

    ASSERT_EQ(world.projectiles().size(), 1u);
    const Vec2 initialPosition = world.projectiles()[0].position();

    GameplayInput noInput{};
    world.update(noInput, 0.1f);

    ASSERT_EQ(world.projectiles().size(), 1u);
    const Vec2 finalPosition = world.projectiles()[0].position();

    EXPECT_FLOAT_EQ(finalPosition.x, initialPosition.x);
    EXPECT_GT(finalPosition.y, initialPosition.y);
}

// 无移动输入时，使用上一次 facing direction 射击
TEST(GameplayWorldTest, FireWithoutMovementUsesPreviousFacingDirection)
{
    GameplayWorld world;

    GameplayInput moveRight{};
    moveRight.moveRight = true;
    world.update(moveRight, 0.0f);

    GameplayInput fire = makeFireInput();
    world.update(fire, 0.0f);

    ASSERT_EQ(world.projectiles().size(), 1u);
    const Vec2 initialPosition = world.projectiles()[0].position();

    GameplayInput noInput{};
    world.update(noInput, 0.1f);

    ASSERT_EQ(world.projectiles().size(), 1u);
    const Vec2 finalPosition = world.projectiles()[0].position();

    EXPECT_GT(finalPosition.x, initialPosition.x);
    EXPECT_FLOAT_EQ(finalPosition.y, initialPosition.y);
}

// 斜向射击
TEST(GameplayWorldTest, FireAfterDiagonalFacingMovesProjectileDiagonally)
{
    GameplayWorld world;
    GameplayInput input = makeFireInput();

    input.moveUp = true;
    input.moveRight = true;
    world.update(input, 0.0f);

    ASSERT_EQ(world.projectiles().size(), 1u);
    const Vec2 initialPosition = world.projectiles()[0].position();

    GameplayInput noInput{};
    world.update(noInput, 0.1f);

    ASSERT_EQ(world.projectiles().size(), 1u);
    const Vec2 finalPosition = world.projectiles()[0].position();

    EXPECT_GT(finalPosition.x, initialPosition.x);
    EXPECT_LT(finalPosition.y, initialPosition.y);
}

// 连续射击时，第一次可以立即射击
TEST(GameplayWorldTest, HoldingFireCreatesFirstProjectileImmediately)
{
    GameplayWorld world;
    GameplayInput input = makeFireInput();

    world.update(input, 0.0f);

    EXPECT_EQ(world.projectiles().size(), 1u);
}

// 按住 Fire 但冷却未结束时，不会再次创建 Projectile
TEST(GameplayWorldTest, HoldingFireDoesNotCreateProjectileBeforeCooldownEnds)
{
    GameplayWorld world;
    GameplayInput input = makeFireInput();

    world.update(input, 0.0f);
    EXPECT_EQ(world.projectiles().size(), 1u);

    input.fireJustPressed = false;
    input.firePressed = true;
    world.update(input, 0.1f);

    EXPECT_EQ(world.projectiles().size(), 1u);
}

// 按住 Fire 且冷却结束后，可以再次生成 Projectile
TEST(GameplayWorldTest, HoldingFireCreatesAnotherProjectileAfterCooldownEnds)
{
    GameplayWorld world;
    GameplayInput input = makeFireInput();

    world.update(input, 0.0f);
    EXPECT_EQ(world.projectiles().size(), 1u);

    input.fireJustPressed = false;
    input.firePressed = true;
    world.update(input, 0.25f);

    EXPECT_EQ(world.projectiles().size(), 2u);
}

// 冷却结束后，如果没有按 Fire，不会自动生成 Projectile
TEST(GameplayWorldTest, NoFireDoesNotCreateProjectileAfterCooldownEnds)
{
    GameplayWorld world;
    GameplayInput input = makeFireInput();

    world.update(input, 0.0f);
    EXPECT_EQ(world.projectiles().size(), 1u);

    GameplayInput noInput{};
    world.update(noInput, 0.25f);

    EXPECT_EQ(world.projectiles().size(), 1u);
}

namespace
{

    constexpr Vec2 kInitialPlayerCenter{
        656.0f,
        376.0f};

    GameplayInput makeInteractInput()
    {
        GameplayInput input{};
        input.interactJustPressed = true;
        return input;
    }

    GameplayWorld makeItemTestWorld(
        std::vector<GroundItemSpawn> spawns,
        InventoryGridSize inventorySize = {10, 6})
    {
        return GameplayWorld{
            3,
            std::move(spawns),
            inventorySize};
    }

} // namespace

TEST(
    GameplayWorldTest,
    InitialInventoryIsEmptyTenBySixGrid)
{
    const GameplayWorld world;

    EXPECT_EQ(
        world.inventory().width(),
        10);
    EXPECT_EQ(
        world.inventory().height(),
        6);
    EXPECT_EQ(
        world.inventory().cellCount(),
        60U);
    EXPECT_TRUE(
        world.inventory()
            .placedItems()
            .empty());
}

TEST(
    GameplayWorldTest,
    InitialGroundItemsHaveStableIds)
{
    const GameplayWorld world;

    ASSERT_EQ(
        world.groundItems().size(),
        6U);

    EXPECT_EQ(
        world.groundItems()[0]
            .item()
            .instanceId(),
        1U);
    EXPECT_EQ(
        world.groundItems()[0]
            .item()
            .definitionId(),
        ItemId::Cola);

    EXPECT_EQ(
        world.groundItems()[1]
            .item()
            .instanceId(),
        2U);
    EXPECT_EQ(
        world.groundItems()[1]
            .item()
            .definitionId(),
        ItemId::Medkit);

    EXPECT_EQ(
        world.groundItems()[2]
            .item()
            .instanceId(),
        3U);
    EXPECT_EQ(
        world.groundItems()[2]
            .item()
            .definitionId(),
        ItemId::Pistol);

    EXPECT_EQ(
        world.groundItems()[3]
            .item()
            .instanceId(),
        4U);
    EXPECT_EQ(
        world.groundItems()[3]
            .item()
            .definitionId(),
        ItemId::Rifle);

    EXPECT_EQ(
        world.groundItems()[4]
            .item()
            .instanceId(),
        5U);
    EXPECT_EQ(
        world.groundItems()[4]
            .item()
            .definitionId(),
        ItemId::Ammo9mm);
    EXPECT_EQ(
        world.groundItems()[4]
            .item()
            .quantity(),
        25U);

    EXPECT_EQ(
        world.groundItems()[5]
            .item()
            .instanceId(),
        6U);
    EXPECT_EQ(
        world.groundItems()[5]
            .item()
            .definitionId(),
        ItemId::Ammo9mm);
    EXPECT_EQ(
        world.groundItems()[5]
            .item()
            .quantity(),
        40U);
    EXPECT_TRUE(
        itemDefinition(ItemId::Ammo9mm)
            .visualAssetsPublished);
}

TEST(
    GameplayWorldTest,
    NoInteractDoesNotPickUpItem)
{
    GameplayWorld world =
        makeItemTestWorld({
            {
                ItemId::Cola,
                kInitialPlayerCenter,
            },
        });

    world.update(
        GameplayInput{},
        0.0f);

    EXPECT_EQ(
        world.groundItems().size(),
        1U);
    EXPECT_TRUE(
        world.inventory()
            .placedItems()
            .empty());
}

TEST(
    GameplayWorldTest,
    InteractOutsideRangeDoesNotPickUpItem)
{
    GameplayWorld world =
        makeItemTestWorld({
            {
                ItemId::Cola,
                Vec2{100.0f, 100.0f},
            },
        });

    world.update(
        makeInteractInput(),
        0.0f);

    EXPECT_EQ(
        world.groundItems().size(),
        1U);
    EXPECT_TRUE(
        world.inventory()
            .placedItems()
            .empty());
}

TEST(
    GameplayWorldTest,
    InteractInRangeTransfersItemIntoInventory)
{
    GameplayWorld world =
        makeItemTestWorld({
            {
                ItemId::Pistol,
                kInitialPlayerCenter,
            },
        });

    ASSERT_EQ(
        world.groundItems().size(),
        1U);

    const ItemInstanceId originalId =
        world.groundItems()
            .front()
            .item()
            .instanceId();

    world.update(
        makeInteractInput(),
        0.0f);

    EXPECT_TRUE(
        world.groundItems().empty());

    ASSERT_EQ(
        world.inventory()
            .placedItems()
            .size(),
        1U);

    const PlacedItem &placed =
        world.inventory()
            .placedItems()
            .front();

    EXPECT_EQ(
        placed.item.instanceId(),
        originalId);
    EXPECT_EQ(
        placed.item.definitionId(),
        ItemId::Pistol);
    EXPECT_EQ(
        placed.origin,
        (GridPosition{0, 0}));

    EXPECT_EQ(
        world.inventory()
            .occupantAt({0, 0}),
        std::optional<ItemInstanceId>{
            originalId});
    EXPECT_EQ(
        world.inventory()
            .occupantAt({1, 0}),
        std::optional<ItemInstanceId>{
            originalId});
}

TEST(
    GameplayWorldTest,
    MultipleCandidatesPickNearestItem)
{
    GameplayWorld world =
        makeItemTestWorld({
            {
                ItemId::Medkit,
                Vec2{680.0f, 376.0f},
            },
            {
                ItemId::Cola,
                Vec2{650.0f, 376.0f},
            },
        });

    world.update(
        makeInteractInput(),
        0.0f);

    ASSERT_EQ(
        world.inventory()
            .placedItems()
            .size(),
        1U);

    EXPECT_EQ(
        world.inventory()
            .placedItems()
            .front()
            .item
            .definitionId(),
        ItemId::Cola);

    ASSERT_EQ(
        world.groundItems().size(),
        1U);

    EXPECT_EQ(
        world.groundItems()
            .front()
            .item()
            .definitionId(),
        ItemId::Medkit);
}

TEST(
    GameplayWorldTest,
    EqualDistanceKeepsEarlierVectorItem)
{
    GameplayWorld world =
        makeItemTestWorld({
            {
                ItemId::Cola,
                Vec2{644.0f, 376.0f},
            },
            {
                ItemId::Medkit,
                Vec2{668.0f, 376.0f},
            },
        });

    world.update(
        makeInteractInput(),
        0.0f);

    ASSERT_EQ(
        world.inventory()
            .placedItems()
            .size(),
        1U);

    EXPECT_EQ(
        world.inventory()
            .placedItems()
            .front()
            .item
            .definitionId(),
        ItemId::Cola);

    ASSERT_EQ(
        world.groundItems().size(),
        1U);

    EXPECT_EQ(
        world.groundItems()
            .front()
            .item()
            .definitionId(),
        ItemId::Medkit);
}

TEST(
    GameplayWorldTest,
    OneInteractPicksAtMostOneItem)
{
    GameplayWorld world =
        makeItemTestWorld({
            {
                ItemId::Cola,
                kInitialPlayerCenter,
            },
            {
                ItemId::Medkit,
                kInitialPlayerCenter,
            },
        });

    world.update(
        makeInteractInput(),
        0.0f);

    EXPECT_EQ(
        world.inventory()
            .placedItems()
            .size(),
        1U);
    EXPECT_EQ(
        world.groundItems().size(),
        1U);
}

TEST(
    GameplayWorldTest,
    NoNewInteractDoesNotPickNextItem)
{
    GameplayWorld world =
        makeItemTestWorld({
            {
                ItemId::Cola,
                kInitialPlayerCenter,
            },
            {
                ItemId::Medkit,
                kInitialPlayerCenter,
            },
        });

    world.update(
        makeInteractInput(),
        0.0f);

    ASSERT_EQ(
        world.inventory()
            .placedItems()
            .size(),
        1U);

    world.update(
        GameplayInput{},
        0.0f);

    EXPECT_EQ(
        world.inventory()
            .placedItems()
            .size(),
        1U);
    EXPECT_EQ(
        world.groundItems().size(),
        1U);
}

TEST(
    GameplayWorldTest,
    PickedItemsUseRowMajorFirstFit)
{
    GameplayWorld world =
        makeItemTestWorld({
            {
                ItemId::Pistol,
                kInitialPlayerCenter,
            },
            {
                ItemId::Medkit,
                kInitialPlayerCenter,
            },
        });

    world.update(
        makeInteractInput(),
        0.0f);

    world.update(
        makeInteractInput(),
        0.0f);

    ASSERT_EQ(
        world.inventory()
            .placedItems()
            .size(),
        2U);

    const PlacedItem &pistol =
        world.inventory()
            .placedItems()[0];

    const PlacedItem &medkit =
        world.inventory()
            .placedItems()[1];

    EXPECT_EQ(
        pistol.item.definitionId(),
        ItemId::Pistol);
    EXPECT_EQ(
        pistol.origin,
        (GridPosition{0, 0}));

    EXPECT_EQ(
        medkit.item.definitionId(),
        ItemId::Medkit);
    EXPECT_EQ(
        medkit.origin,
        (GridPosition{2, 0}));
}

TEST(
    GameplayWorldTest,
    FullInventoryKeepsGroundItemAndInstanceId)
{
    GameplayWorld world =
        makeItemTestWorld(
            {
                {
                    ItemId::Pistol,
                    kInitialPlayerCenter,
                },
                {
                    ItemId::Cola,
                    kInitialPlayerCenter,
                },
            },
            InventoryGridSize{2, 1});

    // Pistol 为 2×1，第一次拾取后填满整个背包。
    world.update(
        makeInteractInput(),
        0.0f);

    ASSERT_EQ(
        world.inventory()
            .placedItems()
            .size(),
        1U);
    ASSERT_EQ(
        world.groundItems().size(),
        1U);

    const ItemInstanceId remainingId =
        world.groundItems()
            .front()
            .item()
            .instanceId();

    ASSERT_TRUE(
        world.groundItems()
            .front()
            .item()
            .valid());

    // 第二次交互时背包已满。
    world.update(
        makeInteractInput(),
        0.0f);

    EXPECT_EQ(
        world.inventory()
            .placedItems()
            .size(),
        1U);

    ASSERT_EQ(
        world.groundItems().size(),
        1U);

    const ItemInstance &remainingItem =
        world.groundItems()
            .front()
            .item();

    EXPECT_TRUE(
        remainingItem.valid());
    EXPECT_EQ(
        remainingItem.instanceId(),
        remainingId);
    EXPECT_EQ(
        remainingItem.definitionId(),
        ItemId::Cola);
}

TEST(
    GameplayWorldTest,
    InventoryCapacityFailureDoesNotChangeOccupiedCells)
{
    GameplayWorld world =
        makeItemTestWorld(
            {
                {
                    ItemId::Pistol,
                    kInitialPlayerCenter,
                },
                {
                    ItemId::Cola,
                    kInitialPlayerCenter,
                },
            },
            InventoryGridSize{2, 1});

    world.update(
        makeInteractInput(),
        0.0f);

    const ItemInstanceId pistolId =
        world.inventory()
            .placedItems()
            .front()
            .item
            .instanceId();

    world.update(
        makeInteractInput(),
        0.0f);

    EXPECT_EQ(
        world.inventory()
            .occupantAt({0, 0}),
        std::optional<ItemInstanceId>{
            pistolId});

    EXPECT_EQ(
        world.inventory()
            .occupantAt({1, 0}),
        std::optional<ItemInstanceId>{
            pistolId});
}

TEST(GameplayWorldTest, OwnsEmptySixBySixContainerInventory)
{
    const GameplayWorld world{
        3,
        std::vector<GroundItemSpawn>{}};

    EXPECT_EQ(world.containerInventory().width(), 6);
    EXPECT_EQ(world.containerInventory().height(), 6);
    EXPECT_TRUE(
        world.containerInventory()
            .placedItems()
            .empty());
}

TEST(GameplayWorldTest, ContainerInventoryBelongsToStorageCabinet)
{
    GameplayWorld world{
        3,
        std::vector<GroundItemSpawn>{}};

    EXPECT_EQ(
        &world.containerInventory(),
        &world.storageCabinet().inventory());
}

TEST(GameplayWorldLootTest, SearchRequiresPlayerInsideCabinetRange)
{
    GameplayWorld world{
        3,
        std::vector<GroundItemSpawn>{}};
    SequenceLootRandomSource random{{0, 0, 0}};

    EXPECT_FALSE(world.searchStorageCabinet(random));
    EXPECT_EQ(random.drawCount(), 0U);
    EXPECT_FALSE(world.storageCabinet().isSearched());
    EXPECT_TRUE(world.containerInventory().placedItems().empty());
}

TEST(GameplayWorldLootTest, FirstSearchCreatesDeterministicRowMajorLoot)
{
    GameplayWorld world{
        3,
        std::vector<GroundItemSpawn>{}};
    movePlayerToCabinet(world);
    SequenceLootRandomSource random{
        {0, 99, 0, 99, 20}};

    ASSERT_TRUE(world.searchStorageCabinet(random));

    EXPECT_TRUE(world.storageCabinet().isSearched());
    EXPECT_EQ(random.drawCount(), 5U);
    ASSERT_EQ(world.containerInventory().placedItems().size(), 2U);

    const PlacedItem &cola =
        world.containerInventory().placedItems()[0];
    const PlacedItem &ammunition =
        world.containerInventory().placedItems()[1];

    EXPECT_EQ(cola.item.instanceId(), 1U);
    EXPECT_EQ(cola.item.definitionId(), ItemId::Cola);
    EXPECT_EQ(cola.item.quantity(), 1U);
    EXPECT_EQ(cola.origin, (GridPosition{0, 0}));

    EXPECT_EQ(ammunition.item.instanceId(), 2U);
    EXPECT_EQ(ammunition.item.definitionId(), ItemId::Ammo9mm);
    EXPECT_EQ(ammunition.item.quantity(), 40U);
    EXPECT_EQ(ammunition.origin, (GridPosition{1, 0}));
}

TEST(GameplayWorldLootTest, ReopeningSearchedCabinetDoesNotReroll)
{
    GameplayWorld world{
        3,
        std::vector<GroundItemSpawn>{}};
    movePlayerToCabinet(world);
    SequenceLootRandomSource random{
        {0, 99, 0, 99, 20}};
    ASSERT_TRUE(world.searchStorageCabinet(random));
    const std::size_t drawCount = random.drawCount();

    EXPECT_TRUE(world.searchStorageCabinet(random));
    EXPECT_EQ(random.drawCount(), drawCount);
    ASSERT_EQ(world.containerInventory().placedItems().size(), 2U);
    EXPECT_EQ(
        world.containerInventory().placedItems()[1].item.quantity(),
        40U);
}

TEST(GameplayWorldLootTest, EmptySearchedCabinetDoesNotReplenish)
{
    GameplayWorld world{
        3,
        std::vector<GroundItemSpawn>{}};
    movePlayerToCabinet(world);
    SequenceLootRandomSource random{
        {0, 99, 0, 99, 20}};
    ASSERT_TRUE(world.searchStorageCabinet(random));
    ASSERT_TRUE(world.containerInventory().remove(1).has_value());
    ASSERT_TRUE(world.containerInventory().remove(2).has_value());
    ASSERT_TRUE(world.containerInventory().placedItems().empty());
    const std::size_t drawCount = random.drawCount();

    EXPECT_TRUE(world.searchStorageCabinet(random));
    EXPECT_EQ(random.drawCount(), drawCount);
    EXPECT_TRUE(world.containerInventory().placedItems().empty());
}

TEST(GameplayWorldLootTest, SearchAdvancesWorldIdOnlyForFinalPlacements)
{
    GameplayWorld world{
        3,
        std::vector<GroundItemSpawn>{}};
    movePlayerToCabinet(world);
    SequenceLootRandomSource random{
        {0, 99, 0, 99, 20}};
    ASSERT_TRUE(world.searchStorageCabinet(random));

    ASSERT_TRUE(world.transferInventoryItemQuantity(
        false,
        2,
        10));
    ASSERT_EQ(world.inventory().placedItems().size(), 1U);
    EXPECT_EQ(
        world.inventory().placedItems().front().item.instanceId(),
        3U);
    EXPECT_EQ(
        world.inventory().placedItems().front().item.quantity(),
        10U);
}

TEST(GameplayWorldLootTest, IdCollisionLeavesCabinetUnsearchedAndIdSequenceUnchanged)
{
    GameplayWorld world{
        3,
        std::vector<GroundItemSpawn>{}};
    movePlayerToCabinet(world);
    ASSERT_TRUE(world.inventory().tryPlace(
        ItemInstance{1, ItemId::Medkit},
        {0, 0}));
    SequenceLootRandomSource firstRandom{
        {0, 99, 0, 99, 20}};

    EXPECT_FALSE(world.searchStorageCabinet(firstRandom));
    EXPECT_FALSE(world.storageCabinet().isSearched());
    EXPECT_TRUE(world.containerInventory().placedItems().empty());
    ASSERT_TRUE(world.inventory().remove(1).has_value());

    SequenceLootRandomSource secondRandom{
        {0, 99, 0, 99, 20}};
    ASSERT_TRUE(world.searchStorageCabinet(secondRandom));
    ASSERT_FALSE(world.containerInventory().placedItems().empty());
    EXPECT_EQ(
        world.containerInventory().placedItems().front().item.instanceId(),
        1U);
}

TEST(GameplayWorldLootTest, DefaultGroundItemsReserveEarlierWorldIds)
{
    GameplayWorld world;
    movePlayerToCabinet(world);
    SequenceLootRandomSource random{
        {0, 99, 0, 99, 20}};

    ASSERT_TRUE(world.searchStorageCabinet(random));
    ASSERT_EQ(world.groundItems().size(), 6U);
    ASSERT_FALSE(world.containerInventory().placedItems().empty());
    EXPECT_EQ(
        world.containerInventory().placedItems().front().item.instanceId(),
        7U);
}

TEST(GameplayWorldLootTest, InvalidRandomResultLeavesWorldStateUnchanged)
{
    GameplayWorld world{
        3,
        std::vector<GroundItemSpawn>{}};
    movePlayerToCabinet(world);
    SequenceLootRandomSource random{{100}};

    EXPECT_THROW(
        static_cast<void>(
            world.searchStorageCabinet(random)),
        std::out_of_range);
    EXPECT_FALSE(world.storageCabinet().isSearched());
    EXPECT_TRUE(world.containerInventory().placedItems().empty());

    SequenceLootRandomSource validRandom{
        {0, 99, 0, 99, 20}};
    ASSERT_TRUE(world.searchStorageCabinet(validRandom));
    ASSERT_FALSE(world.containerInventory().placedItems().empty());
    EXPECT_EQ(
        world.containerInventory().placedItems().front().item.instanceId(),
        1U);
}

TEST(GameplayWorldTest, PlayerMustApproachCabinetBeforeInteraction)
{
    GameplayWorld world{
        3,
        std::vector<GroundItemSpawn>{}};

    EXPECT_FALSE(world.canInteractWithContainer());

    GameplayInput moveRight{};
    moveRight.moveRight = true;
    world.update(moveRight, 1.0F);

    EXPECT_TRUE(world.canInteractWithContainer());
}

TEST(GameplayWorldTest, DropsPlayerInventoryItemAtPlayersFeet)
{
    GameplayWorld world{
        3,
        std::vector<GroundItemSpawn>{}};
    ItemInstance rifle{501, ItemId::Rifle};

    ASSERT_TRUE(
        world.inventory().tryPlace(
            std::move(rifle),
            {2, 1}));

    ASSERT_TRUE(world.dropInventoryItem(501));

    EXPECT_TRUE(world.inventory().placedItems().empty());
    ASSERT_EQ(world.groundItems().size(), 1U);

    const GroundItem &dropped = world.groundItems().front();
    EXPECT_EQ(dropped.item().instanceId(), 501U);
    EXPECT_EQ(dropped.item().definitionId(), ItemId::Rifle);
    EXPECT_FLOAT_EQ(dropped.position().x, 656.0f);
    EXPECT_FLOAT_EQ(dropped.position().y, 392.0f);
}

TEST(GameplayWorldTest, MissingDropIdLeavesWorldUnchanged)
{
    GameplayWorld world{
        3,
        std::vector<GroundItemSpawn>{}};
    ItemInstance cola{502, ItemId::Cola};

    ASSERT_TRUE(
        world.inventory().tryPlace(
            std::move(cola),
            {0, 0}));

    EXPECT_FALSE(world.dropInventoryItem(999));
    ASSERT_EQ(world.inventory().placedItems().size(), 1U);
    EXPECT_EQ(
        world.inventory()
            .placedItems()
            .front()
            .item.instanceId(),
        502U);
    EXPECT_TRUE(world.groundItems().empty());
}

TEST(GameplayWorldTest, CannotDropDirectlyFromContainerInventory)
{
    GameplayWorld world{
        3,
        std::vector<GroundItemSpawn>{}};
    ItemInstance medkit{503, ItemId::Medkit};

    ASSERT_TRUE(
        world.containerInventory().tryPlace(
            std::move(medkit),
            {1, 1}));

    EXPECT_FALSE(world.dropInventoryItem(503));
    ASSERT_EQ(
        world.containerInventory().placedItems().size(),
        1U);
    EXPECT_TRUE(world.groundItems().empty());
}

TEST(GameplayWorldTest, DropPositionKeepsItemInsideWorldBounds)
{
    GameplayWorld world{
        3,
        std::vector<GroundItemSpawn>{}};

    GameplayInput moveDown{};
    moveDown.moveDown = true;
    world.update(moveDown, 10.0f);

    ItemInstance cola{504, ItemId::Cola};
    ASSERT_TRUE(
        world.inventory().tryPlace(
            std::move(cola),
            {0, 0}));

    ASSERT_TRUE(world.dropInventoryItem(504));
    ASSERT_EQ(world.groundItems().size(), 1U);

    EXPECT_FLOAT_EQ(
        world.groundItems().front().position().y,
        704.0f);
}

TEST(GameplayWorldTest, DropPositionIgnoresRightFacingDirection)
{
    GameplayWorld world{
        3,
        std::vector<GroundItemSpawn>{}};

    GameplayInput moveRight{};
    moveRight.moveRight = true;
    world.update(moveRight, 0.0f);

    ItemInstance cola{505, ItemId::Cola};
    ASSERT_TRUE(
        world.inventory().tryPlace(
            std::move(cola),
            {0, 0}));

    ASSERT_TRUE(world.dropInventoryItem(505));
    ASSERT_EQ(world.groundItems().size(), 1U);

    EXPECT_FLOAT_EQ(
        world.groundItems().front().position().x,
        656.0f);
    EXPECT_FLOAT_EQ(
        world.groundItems().front().position().y,
        392.0f);
}

TEST(GameplayWorldTest, DropPositionIgnoresDownFacingDirection)
{
    GameplayWorld world{
        3,
        std::vector<GroundItemSpawn>{}};

    GameplayInput moveDown{};
    moveDown.moveDown = true;
    world.update(moveDown, 0.0f);

    ItemInstance cola{506, ItemId::Cola};
    ASSERT_TRUE(
        world.inventory().tryPlace(
            std::move(cola),
            {0, 0}));

    ASSERT_TRUE(world.dropInventoryItem(506));
    ASSERT_EQ(world.groundItems().size(), 1U);

    EXPECT_FLOAT_EQ(
        world.groundItems().front().position().x,
        656.0f);
    EXPECT_FLOAT_EQ(
        world.groundItems().front().position().y,
        392.0f);
}

TEST(GameplayWorldTest, DropPositionIgnoresLeftFacingDirection)
{
    GameplayWorld world{
        3,
        std::vector<GroundItemSpawn>{}};

    GameplayInput moveLeft{};
    moveLeft.moveLeft = true;
    world.update(moveLeft, 0.0f);

    ItemInstance cola{507, ItemId::Cola};
    ASSERT_TRUE(
        world.inventory().tryPlace(
            std::move(cola),
            {0, 0}));

    ASSERT_TRUE(world.dropInventoryItem(507));
    ASSERT_EQ(world.groundItems().size(), 1U);

    EXPECT_FLOAT_EQ(
        world.groundItems().front().position().x,
        656.0f);
    EXPECT_FLOAT_EQ(
        world.groundItems().front().position().y,
        392.0f);
}

TEST(GameplayWorldTest, DropCommitsRequestedItemOrientation)
{
    GameplayWorld world{
        3,
        {},
        InventoryGridSize{10, 6}};
    ItemInstance rifle{508, ItemId::Rifle};
    ASSERT_TRUE(
        world.inventory().tryPlace(
            std::move(rifle),
            {0, 0}));

    ASSERT_TRUE(world.dropInventoryItem(
        508,
        ItemOrientation::Degrees90));

    ASSERT_EQ(world.groundItems().size(), 1U);
    EXPECT_EQ(
        world.groundItems().front().item().orientation(),
        ItemOrientation::Degrees90);
    EXPECT_TRUE(world.inventory().placedItems().empty());
}

TEST(GameplayWorldStackTest, PickupMergesWholeGroundStackAtomically)
{
    GameplayWorld world{
        3,
        {{ItemId::Ammo9mm, {640.0F, 360.0F}, 2}},
        InventoryGridSize{1, 1}};
    ItemInstance ammo{700, ItemId::Ammo9mm, 58};
    ASSERT_TRUE(world.inventory().tryPlace(std::move(ammo), {0, 0}));

    GameplayInput pickup{};
    pickup.interactJustPressed = true;
    world.update(pickup, 0.0F);

    EXPECT_TRUE(world.groundItems().empty());
    ASSERT_EQ(world.inventory().placedItems().size(), 1U);
    EXPECT_EQ(world.inventory().placedItems().front().item.instanceId(), 700U);
    EXPECT_EQ(world.inventory().placedItems().front().item.quantity(), 60U);
}

TEST(GameplayWorldStackTest, FailedPickupPreservesGroundAndInventoryQuantities)
{
    GameplayWorld world{
        3,
        {{ItemId::Ammo9mm, {640.0F, 360.0F}, 1}},
        InventoryGridSize{1, 1}};
    ItemInstance ammo{701, ItemId::Ammo9mm, 60};
    ASSERT_TRUE(world.inventory().tryPlace(std::move(ammo), {0, 0}));

    GameplayInput pickup{};
    pickup.interactJustPressed = true;
    world.update(pickup, 0.0F);

    ASSERT_EQ(world.groundItems().size(), 1U);
    EXPECT_EQ(world.groundItems().front().item().quantity(), 1U);
    EXPECT_EQ(world.inventory().placedItems().front().item.quantity(), 60U);
}

TEST(GameplayWorldStackTest, PartialContainerTransferConsumesWorldIdOnCommit)
{
    GameplayWorld world{
        3,
        {},
        InventoryGridSize{2, 1}};
    ItemInstance ammo{702, ItemId::Ammo9mm, 10};
    ASSERT_TRUE(world.inventory().tryPlace(std::move(ammo), {0, 0}));

    ASSERT_TRUE(world.transferInventoryItemQuantity(true, 702, 4));

    ASSERT_EQ(world.inventory().placedItems().size(), 1U);
    EXPECT_EQ(world.inventory().placedItems().front().item.quantity(), 6U);
    ASSERT_EQ(world.containerInventory().placedItems().size(), 1U);
    EXPECT_EQ(
        world.containerInventory().placedItems().front().item.instanceId(),
        1U);
    EXPECT_EQ(
        world.containerInventory().placedItems().front().item.quantity(),
        4U);
}

TEST(GameplayWorldStackTest, PickupMergesThenPlacesRemainderWithGroundId)
{
    GameplayWorld world{
        3,
        {{ItemId::Ammo9mm, {640.0F, 360.0F}, 5}},
        InventoryGridSize{2, 1}};
    ItemInstance ammo{703, ItemId::Ammo9mm, 59};
    ASSERT_TRUE(world.inventory().tryPlace(std::move(ammo), {0, 0}));

    GameplayInput pickup{};
    pickup.interactJustPressed = true;
    world.update(pickup, 0.0F);

    EXPECT_TRUE(world.groundItems().empty());
    ASSERT_EQ(world.inventory().placedItems().size(), 2U);
    EXPECT_EQ(world.inventory().placedItems()[0].item.instanceId(), 703U);
    EXPECT_EQ(world.inventory().placedItems()[0].item.quantity(), 60U);
    EXPECT_EQ(world.inventory().placedItems()[1].item.instanceId(), 1U);
    EXPECT_EQ(world.inventory().placedItems()[1].item.quantity(), 4U);
    EXPECT_EQ(world.inventory().placedItems()[1].origin, (GridPosition{1, 0}));
}

TEST(GameplayWorldStackTest, DropAndPickupPreserveWholeStackQuantityAndId)
{
    GameplayWorld world{
        3,
        {},
        InventoryGridSize{2, 1}};
    ItemInstance ammo{704, ItemId::Ammo9mm, 25};
    ASSERT_TRUE(world.inventory().tryPlace(std::move(ammo), {0, 0}));

    ASSERT_TRUE(world.dropInventoryItem(704));
    ASSERT_EQ(world.groundItems().size(), 1U);
    EXPECT_EQ(world.groundItems().front().item().instanceId(), 704U);
    EXPECT_EQ(world.groundItems().front().item().quantity(), 25U);

    GameplayInput pickup{};
    pickup.interactJustPressed = true;
    world.update(pickup, 0.0F);

    EXPECT_TRUE(world.groundItems().empty());
    ASSERT_EQ(world.inventory().placedItems().size(), 1U);
    EXPECT_EQ(world.inventory().placedItems().front().item.instanceId(), 704U);
    EXPECT_EQ(world.inventory().placedItems().front().item.quantity(), 25U);
}

TEST(GameplayWorldStackTest, PartialDropCreatesGroundStackAtPlayersFeet)
{
    GameplayWorld world{
        3,
        {},
        InventoryGridSize{2, 1}};
    ItemInstance ammo{705, ItemId::Ammo9mm, 25};
    ASSERT_TRUE(world.inventory().tryPlace(std::move(ammo), {0, 0}));

    ASSERT_TRUE(world.dropInventoryItemQuantity(
        705,
        5,
        ItemOrientation::Degrees0));

    EXPECT_EQ(world.inventory().quantityOf(705), 20U);
    ASSERT_EQ(world.groundItems().size(), 1U);
    EXPECT_EQ(world.groundItems().front().item().instanceId(), 1U);
    EXPECT_EQ(world.groundItems().front().item().quantity(), 5U);
    EXPECT_FLOAT_EQ(world.groundItems().front().position().x, 656.0F);
    EXPECT_FLOAT_EQ(world.groundItems().front().position().y, 392.0F);
}

TEST(GameplayWorldStackTest, QuantityPlacementWorksInsidePlayerInventory)
{
    GameplayWorld world{
        3,
        {},
        InventoryGridSize{3, 1}};
    ItemInstance ammo{706, ItemId::Ammo9mm, 9};
    ASSERT_TRUE(world.inventory().tryPlace(std::move(ammo), {0, 0}));

    ASSERT_TRUE(world.placeInventoryItemQuantity(
        true,
        true,
        706,
        1,
        {2, 0},
        ItemOrientation::Degrees0));

    EXPECT_EQ(world.inventory().quantityOf(706), 8U);
    EXPECT_EQ(world.inventory().quantityOf(1), 1U);
    EXPECT_EQ(world.inventory().originOf(1),
              (std::optional<GridPosition>{GridPosition{2, 0}}));
}

TEST(GameplayWorldStackTest, FailedQuantityPlacementDoesNotConsumeWorldId)
{
    GameplayWorld world{
        3,
        {},
        InventoryGridSize{3, 1}};
    ItemInstance sourceAmmo{707, ItemId::Ammo9mm, 10};
    ItemInstance fullAmmo{708, ItemId::Ammo9mm, 60};
    ASSERT_TRUE(world.inventory().tryPlace(std::move(sourceAmmo), {0, 0}));
    ASSERT_TRUE(world.inventory().tryPlace(std::move(fullAmmo), {1, 0}));

    EXPECT_FALSE(world.placeInventoryItemQuantity(
        true,
        true,
        707,
        1,
        {1, 0},
        ItemOrientation::Degrees0));
    ASSERT_TRUE(world.placeInventoryItemQuantity(
        true,
        true,
        707,
        1,
        {2, 0},
        ItemOrientation::Degrees0));

    EXPECT_EQ(world.inventory().quantityOf(707), 9U);
    EXPECT_EQ(world.inventory().quantityOf(1), 1U);
    EXPECT_EQ(world.inventory().originOf(2), std::nullopt);
}

TEST(GameplayWorldStackTest, PartialDropRejectsConflictingWorldId)
{
    GameplayWorld world{
        3,
        {},
        InventoryGridSize{2, 1}};
    ItemInstance ammo{1, ItemId::Ammo9mm, 10};
    ASSERT_TRUE(world.inventory().tryPlace(std::move(ammo), {0, 0}));

    EXPECT_FALSE(world.dropInventoryItemQuantity(
        1,
        1,
        ItemOrientation::Degrees0));

    EXPECT_EQ(world.inventory().quantityOf(1), 10U);
    EXPECT_TRUE(world.groundItems().empty());
}
