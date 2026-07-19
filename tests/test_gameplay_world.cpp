#include <gtest/gtest.h>

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

// Projectile 可以命中移动的 Enemy
TEST(GameplayWorldTest, ProjectileCanHitMovingEnemy)
{
    GameplayWorld world;

    GameplayInput fire = makeFireInput();
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
    constexpr float kExpectedImpactX{656.0f};
    constexpr float kExpectedImpactY{140.0f};

    void createDefaultProjectileHit(GameplayWorld &world)
    {
        GameplayInput fire = makeFireInput();
        world.update(fire, 0.0f);

        GameplayInput noInput{};
        world.update(noInput, 0.35f);
    }
}

// GameplayWorld 更新 Enemy 时，也应推进其移动动画。
// 这保证 App 能从 world.enemies() 读取有效 frame index。
TEST(
    GameplayWorldTest,
    EnemyAnimationAdvancesThroughWorldUpdate)
{
    GameplayWorld world;
    GameplayInput input{};

    ASSERT_EQ(world.enemies().size(), 1u);

    const Enemy &initialEnemy = world.enemies()[0];

    EXPECT_EQ(
        initialEnemy.facingDirection(),
        EnemyFacingDirection::Right);
    EXPECT_EQ(
        initialEnemy.currentAnimationFrameIndex(),
        0u);

    // Enemy 每帧动画持续 0.125 秒。
    world.update(input, 0.125f);

    ASSERT_EQ(world.enemies().size(), 1u);

    const Enemy &updatedEnemy = world.enemies()[0];

    EXPECT_EQ(
        updatedEnemy.facingDirection(),
        EnemyFacingDirection::Right);
    EXPECT_EQ(
        updatedEnemy.currentAnimationFrameIndex(),
        1u);
}

// Enemy 在 GameplayWorld 中撞到右边界后，
// App 应能读取到 Left 方向用于选择 sprite-sheet 行。
TEST(
    GameplayWorldTest,
    EnemyBounceExposesLeftFacingDirectionForRendering)
{
    GameplayWorld world;
    GameplayInput input{};

    world.update(input, 10.0f);

    ASSERT_EQ(world.enemies().size(), 1u);

    const Enemy &enemy = world.enemies()[0];

    EXPECT_FLOAT_EQ(enemy.position().x, 1230.0f);
    EXPECT_LT(enemy.velocity().x, 0.0f);
    EXPECT_EQ(
        enemy.facingDirection(),
        EnemyFacingDirection::Left);

    // 防止渲染层得到非法 source frame。
    EXPECT_LT(
        enemy.currentAnimationFrameIndex(),
        6u);
}

TEST(GameplayWorldTest, ProjectileHitCreatesParticles)
{
    GameplayWorld world;

    createDefaultProjectileHit(world);

    ASSERT_EQ(world.particles().size(), 12u);

    for (const Particle &particle : world.particles())
    {
        EXPECT_FLOAT_EQ(particle.position().x, kExpectedImpactX);
        EXPECT_FLOAT_EQ(particle.position().y, kExpectedImpactY);
        EXPECT_FLOAT_EQ(
            particle.remainingLifetime(),
            particle.duration());
    }
}

TEST(GameplayWorldTest, ExpiredParticlesAreRemovedByWorldUpdate)
{
    GameplayWorld world;

    createDefaultProjectileHit(world);

    ASSERT_EQ(world.particles().size(), 12u);

    GameplayInput noInput{};
    world.update(noInput, 0.50f);

    EXPECT_TRUE(world.particles().empty());
}