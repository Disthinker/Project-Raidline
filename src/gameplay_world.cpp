#include "gameplay_world.h"

#include <algorithm>

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
}

GameplayWorld::GameplayWorld()
    : GameplayWorld{kDefaultEnemyMaxHealth}
{
}

GameplayWorld::GameplayWorld(int enemyMaxHealth)
    : particleSystem_{
          0xC0FFEEu,
          ParticleBurstConfig{}}
{
    enemies_.emplace_back(
        Vec2{600.0f, 100.0f},
        Vec2{50.0f, 50.0f},
        Vec2{150.0f, 0.0f},
        enemyMaxHealth);
}

void GameplayWorld::update(
    const GameplayInput &input,
    float deltaTime)
{
    // 先更新上一帧已经存在的粒子。
    particleSystem_.update(deltaTime);

    player_.update(
        input,
        deltaTime,
        kWorldWidth,
        kWorldHeight);

    for (Enemy &enemy : enemies_)
    {
        enemy.update(
            deltaTime,
            kWorldWidth);
    }

    cooldownRemaining_ -= deltaTime;

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
            Vec2{projectileX, projectileY},
            projectileVelocity,
            kProjectileWidth,
            kProjectileHeight,
            kProjectileDamage);

        cooldownRemaining_ = fireCooldown_;
    }

    for (Projectile &projectile : projectiles_)
    {
        projectile.update(deltaTime);
    }

    const HitResolutionResult hitResult =
        resolveProjectileEnemyHits(
            projectiles_,
            enemies_);

    // 每次有效命中都生成粒子，
    // 与本次命中是否致命无关。
    for (const Vec2 &position : hitResult.hitPositions)
    {
        particleSystem_.emitImpact(position);
    }

    // enemiesKilled 是 std::size_t。
    // Score 使用 int，因此在职责边界上显式转换。
    const int killedEnemyCount =
        static_cast<int>(hitResult.enemiesKilled);

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

const Player &GameplayWorld::player() const
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

int GameplayWorld::score() const noexcept
{
    return score_;
}