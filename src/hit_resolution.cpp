#include "hit_resolution.h"
#include <cstddef>
#include <algorithm>
#include "collision.h"

void resolveProjectileEnemyHits(std::vector<Enemy> &enemies_, std::vector<Projectile> &projectiles_)
{
    std::vector<bool> projectileHit{};
    std::vector<bool> enemiesHit{};
    projectileHit.resize(projectiles_.size(), false);
    enemiesHit.resize(enemies_.size(), false);
    std::size_t projectileIndex{0};
    std::size_t enemyIndex{0};

    for (projectileIndex = 0; projectileIndex < projectiles_.size(); ++projectileIndex)
    {
        if (projectileHit[projectileIndex])
        {
            continue;
        }
        for (enemyIndex = 0; enemyIndex < enemies_.size(); ++enemyIndex)
        {
            if (enemiesHit[enemyIndex])
            {
                continue;
            }

            if (isCollision(projectiles_[projectileIndex].bounds(), enemies_[enemyIndex].bounds()))
            {
                projectileHit[projectileIndex] = true;
                enemiesHit[enemyIndex] = true;
                // Handle the hit, e.g., reduce enemy health or destroy projectile
                break;
            }
        }
    }
    // Remove projectiles that have been hit
    std::vector<Projectile> newProjectiles{};
    for (projectileIndex = 0; projectileIndex < projectiles_.size(); ++projectileIndex)
    {
        if (!projectileHit[projectileIndex])
        {
            newProjectiles.push_back(projectiles_[projectileIndex]);
        }
    }
    projectiles_ = newProjectiles;
    // Remove enemies that have been hit
    std::vector<Enemy> newEnemies{};
    for (enemyIndex = 0; enemyIndex < enemies_.size(); ++enemyIndex)
    {
        if (!enemiesHit[enemyIndex])
        {
            newEnemies.push_back(enemies_[enemyIndex]);
        }
    }
    enemies_ = newEnemies;
}