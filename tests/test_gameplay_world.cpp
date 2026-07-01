#include <gtest/gtest.h>

#include "gameplay_input.h"
#include "gameplay_world.h"

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
    GameplayInput input{};
    input.fireJustPressed = true;

    world.update(input, 0.0f);

    ASSERT_EQ(world.projectiles().size(), 1u);
    const Projectile &projectile = world.projectiles()[0];

    EXPECT_FLOAT_EQ(projectile.position().x, 652.0f);
    EXPECT_FLOAT_EQ(projectile.position().y, 340.0f);
    EXPECT_FLOAT_EQ(projectile.width(), 8.0f);
    EXPECT_FLOAT_EQ(projectile.height(), 20.0f);
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

    GameplayInput fire{};
    fire.fireJustPressed = true;
    world.update(fire, 0.0f);

    const float initialY = world.projectiles()[0].position().y;

    GameplayInput noInput{};
    world.update(noInput, 0.1f);

    ASSERT_EQ(world.projectiles().size(), 1u);
    EXPECT_LT(world.projectiles()[0].position().y, initialY);
}

// Projectile 可以命中移动的 Enemy
TEST(GameplayWorldTest, ProjectileCanHitMovingEnemy)
{
    GameplayWorld world;

    GameplayInput fire{};
    fire.fireJustPressed = true;
    world.update(fire, 0.0f);
    ASSERT_EQ(world.projectiles().size(), 1u);
    ASSERT_EQ(world.enemies().size(), 1u);

    GameplayInput noInput{};
    world.update(noInput, 0.35f);

    EXPECT_TRUE(world.projectiles().empty());
    EXPECT_TRUE(world.enemies().empty());
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
    GameplayInput input{};

    input.moveRight = true;
    input.fireJustPressed = true;
    world.update(input, 0.0f);
    const Vec2 initialPosition = world.projectiles()[0].position();

    input.moveRight = false;
    input.fireJustPressed = false;
    world.update(input, 0.1f);
    const Vec2 finalPosition = world.projectiles()[0].position();

    EXPECT_GT(finalPosition.x, initialPosition.x);
    EXPECT_FLOAT_EQ(finalPosition.y, initialPosition.y);
}
// 左朝向射击
TEST(GameplayWorldTest, FireAfterFacingLeftMovesProjectileLeft)
{
    GameplayWorld world;
    GameplayInput input{};

    input.moveLeft = true;
    input.fireJustPressed = true;
    world.update(input, 0.0f);
    const Vec2 initialPosition = world.projectiles()[0].position();

    input.moveLeft = false;
    input.fireJustPressed = false;
    world.update(input, 0.1f);
    const Vec2 finalPosition = world.projectiles()[0].position();

    EXPECT_FLOAT_EQ(finalPosition.y, initialPosition.y);
    EXPECT_LT(finalPosition.x, initialPosition.x);
}
// 下朝向射击
TEST(GameplayWorldTest, FireAfterFacingDownMovesProjectileDown)
{
    GameplayWorld world;
    GameplayInput input{};

    input.moveDown = true;
    input.fireJustPressed = true;
    world.update(input, 0.0f);
    const Vec2 initialPosition = world.projectiles()[0].position();

    input.moveDown = false;
    input.fireJustPressed = false;
    world.update(input, 0.1f);
    const Vec2 finalPosition = world.projectiles()[0].position();

    EXPECT_FLOAT_EQ(finalPosition.x, initialPosition.x);
    EXPECT_GT(finalPosition.y, initialPosition.y);
}
// 旧朝向射击
TEST(GameplayWorldTest, FireWithoutMovementUsesPreviousFacingDirection)
{
    GameplayWorld world;
    GameplayInput input{};

    input.moveRight = true;
    world.update(input, 1.0f);

    input.moveRight = false;
    input.fireJustPressed = true;
    world.update(input, 0.0f);
    const Vec2 initialPosition = world.projectiles()[0].position();

    input.fireJustPressed = false;
    world.update(input, 0.1f);
    const Vec2 finalPosition = world.projectiles()[0].position();

    EXPECT_GT(finalPosition.x, initialPosition.x);
    EXPECT_FLOAT_EQ(finalPosition.y, initialPosition.y);
}

// 斜向射击
TEST(GameplayWorldTest, FireAfterDiagonalFacingMovesProjectileDiagonally)
{
    GameplayWorld world;
    GameplayInput input{};

    input.moveUp = true;
    input.moveRight = true;
    world.update(input, 1.0f);

    input.fireJustPressed = true;
    world.update(input, 0.0f);
    const Vec2 initialPosition = world.projectiles()[0].position();

    input.fireJustPressed = false;
    world.update(input, 0.1f);
    const Vec2 finalPosition = world.projectiles()[0].position();

    EXPECT_GT(finalPosition.x, initialPosition.x);
    EXPECT_LT(finalPosition.y, initialPosition.y);
}