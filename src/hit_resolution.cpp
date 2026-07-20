#include "hit_resolution.h"

#include <algorithm>
#include <cstddef>
#include <vector>

#include "collision.h"

HitResolutionResult resolveProjectileEnemyHits(
    std::vector<Projectile> &projectiles,
    std::vector<Enemy> &enemies)
{
    HitResolutionResult result{};

    std::vector<bool> projectileConsumed(
        projectiles.size(),
        false);

    for (
        std::size_t projectileIndex{0};
        projectileIndex < projectiles.size();
        ++projectileIndex)
    {
        const Projectile &projectile =
            projectiles[projectileIndex];

        for (Enemy &enemy : enemies)
        {
            // Enemy 可能已被同一帧内更早的 Projectile 击杀。
            // 在统一删除前，它仍暂时留在容器中，因此必须跳过。
            if (enemy.isDead())
            {
                continue;
            }

            if (!isCollision(
                    projectile.bounds(),
                    enemy.bounds()))
            {
                continue;
            }

            // 一枚 Projectile 一次最多命中一个 Enemy。
            projectileConsumed[projectileIndex] = true;

            result.hitPositions.push_back(
                Vec2{
                    projectile.position().x +
                        projectile.width() / 2.0f,
                    projectile.position().y +
                        projectile.height() / 2.0f});

            const bool killed =
                enemy.takeDamage(projectile.damage());

            if (killed)
            {
                ++result.enemiesKilled;
            }

            break;
        }
    }

    std::size_t projectileEraseIndex{0};

    std::erase_if(
        projectiles,
        [&](const Projectile &)
        {
            const bool shouldErase =
                projectileConsumed[projectileEraseIndex];

            ++projectileEraseIndex;
            return shouldErase;
        });

    std::erase_if(
        enemies,
        [](const Enemy &enemy)
        {
            return enemy.isDead();
        });

    return result;
}