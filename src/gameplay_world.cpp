#include <algorithm>
#include "hit_resolution.h"
#include "gameplay_world.h"

namespace
{
    constexpr float kWorldWidth{1280.0f};
    constexpr float kWorldHeight{720.0f};

    constexpr float kProjectileSpeed{600.0f};
    constexpr float kProjectileWidth{8.0f};
    constexpr float kProjectileHeight{20.0f};

    constexpr float kHitEffectLifetime{0.15f};
    constexpr float kHitEffectSize{16.0f};
}

GameplayWorld::GameplayWorld()
    : particleSystem_{0xC0FFEEu, ParticleBurstConfig{}}
{
    enemies_.emplace_back(Vec2{600.0f, 100.0f}, Vec2{50.0f, 50.0f}, Vec2{150.0f, 0.0f});
}

void GameplayWorld::update(const GameplayInput &input, float deltaTime)
{
    // 更新粒子
    particleSystem_.update(deltaTime);
    // 更新命中反馈
    for (auto &hitEffect : hitEffects_)
    {
        hitEffect.update(deltaTime);
    }
    hitEffects_.erase(std::remove_if(hitEffects_.begin(), hitEffects_.end(),
                                     [](const HitEffect &effect)
                                     { return effect.isExpired(); }),
                      hitEffects_.end());

    player_.update(input, deltaTime, kWorldWidth, kWorldHeight);

    for (auto &enemy : enemies_)
    {
        enemy.update(deltaTime, kWorldWidth);
    }
    cooldownRemaining_ -= deltaTime;
    if (input.firePressed && cooldownRemaining_ <= 0.0f)
    {
        const float projectileX = player_.position().x + player_.size() / 2 - kProjectileWidth / 2;
        const float projectileY = player_.position().y - kProjectileHeight;
        Vec2 projectileVelocity = {player_.facingDirection().x * kProjectileSpeed, player_.facingDirection().y * kProjectileSpeed};
        projectiles_.emplace_back(Vec2{projectileX, projectileY}, projectileVelocity, kProjectileWidth, kProjectileHeight);
        cooldownRemaining_ = fireCooldown_; // Reset cooldown after firing
    }

    for (auto &projectile : projectiles_)
    {
        projectile.update(deltaTime);
    }

    const HitResolutionResult hitResult = resolveProjectileEnemyHits(projectiles_, enemies_);

    for (auto &position : hitResult.hitPositions)
    {
        hitEffects_.emplace_back(position, kHitEffectLifetime, kHitEffectSize);
    }

    projectiles_.erase(
        std::remove_if(
            projectiles_.begin(),
            projectiles_.end(),
            [](const Projectile &projectile)
            {
                return projectile.isOutside(kWorldWidth, kWorldHeight);
            }),
        projectiles_.end());
}

const Player &GameplayWorld::player() const
{
    return player_;
}

const std::vector<Projectile> &GameplayWorld::projectiles() const
{
    return projectiles_;
}

const std::vector<Enemy> &GameplayWorld::enemies() const
{
    return enemies_;
}

const std::vector<HitEffect> &GameplayWorld::hitEffects() const
{
    return hitEffects_;
}

const std::vector<Particle> &GameplayWorld::particles() const
{
    return particleSystem_.particles();
}