#include <algorithm>
#include "hit_resolution.h"
#include "gameplay_world.h"

namespace
{
    constexpr float kWorldWidth{1280.0f};
    constexpr float kWorldHeight{720.0f};

    constexpr Vec2 kProjectileVelocity{0.0f, -600.0f};
    constexpr float kProjectileWidth{8.0f};
    constexpr float kProjectileHeight{20.0f};
}

GameplayWorld::GameplayWorld()
{
    enemies_.emplace_back(Vec2{600.0f, 100.0f}, Vec2{50.0f, 50.0f});
}

void GameplayWorld::update(const GameplayInput &input, float deltaTime)
{
    player_.update(input, deltaTime, kWorldWidth, kWorldHeight);

    if (input.fireJustPressed)
    {
        const float projectileX = player_.position().x + player_.size() / 2 - kProjectileWidth / 2;
        const float projectileY = player_.position().y - kProjectileHeight;
        projectiles_.emplace_back(Vec2{projectileX, projectileY}, kProjectileVelocity, kProjectileWidth, kProjectileHeight);
    }

    for (auto &projectile : projectiles_)
    {
        projectile.update(deltaTime);
    }

    resolveProjectileEnemyHits(projectiles_, enemies_);

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
