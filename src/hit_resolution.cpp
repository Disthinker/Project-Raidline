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
                break;
            }
        }
    }
    std::size_t projectileIndex = 0;
    std::erase_if(projectiles_, [&](const Projectile &)
                  { return projectileHit[projectileIndex++] != 0; });

    std::size_t enemyIndex = 0;
    std::erase_if(enemies_, [&](const Enemy &)
                  { return enemiesHit[enemyIndex++] != 0; });
}