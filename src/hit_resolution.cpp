#include "hit_resolution.h"
#include "collision.h"

void hitResolution(std::vector<Enemy> &enemies_, std::vector<Projectile> &projectiles_)
{
    std::vector<bool> projectileHit{};
    std::vector<bool> enemiesHit{};
    projectileHit.resize(projectiles_.size(), false);
    enemiesHit.resize(enemies_.size(), false);
    int iii{0};
    int jjj{0};

    for (iii = 0; iii < projectiles_.size(); ++iii)
    {
        if (projectileHit[iii])
        {
            continue;
        }
        for (jjj = 0; jjj < enemies_.size(); ++jjj)
        {
            if (enemiesHit[jjj])
            {
                continue;
            }

            if (isCollision(projectiles_[iii].bounds(), enemies_[jjj].bounds()))
            {
                projectileHit[iii] = true;
                enemiesHit[jjj] = true;
                // Handle the hit, e.g., reduce enemy health or destroy projectile
                break;
            }
        }
    }
    // Remove projectiles that have been hit
    std::vector<Projectile> newProjectiles{};
    for (iii = 0; iii < projectiles_.size(); ++iii)
    {
        if (!projectileHit[iii])
        {
            newProjectiles.push_back(projectiles_[iii]);
        }
    }
    projectiles_ = newProjectiles;
    // Remove enemies that have been hit
    std::vector<Enemy> newEnemies{};
    for (jjj = 0; jjj < enemies_.size(); ++jjj)
    {
        if (!enemiesHit[jjj])
        {
            newEnemies.push_back(enemies_[jjj]);
        }
    }
    enemies_ = newEnemies;
}