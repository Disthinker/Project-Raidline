#include <gtest/gtest.h>
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
        4U);

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