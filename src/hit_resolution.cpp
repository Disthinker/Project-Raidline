#include "hit_resolution.h"

#include <algorithm>
#include <cstddef>
#include <optional>
#include <vector>

#include "collision.h"

namespace
{
    HitRegion hitRegionAt(
        const Rect &target,
        Vec2 hitPosition) noexcept
    {
        const float relativeY =
            (hitPosition.y - target.position.y) / target.size.y;
        if (relativeY < 0.25F)
        {
            return HitRegion::Head;
        }
        if (relativeY < 0.75F)
        {
            return HitRegion::Torso;
        }
        return HitRegion::Legs;
    }
}

HitResolutionResult resolveShotEnemyHits(
    const std::vector<ShotCollisionCandidate> &shots,
    std::vector<Enemy> &enemies)
{
    HitResolutionResult result{};

    for (const ShotCollisionCandidate &shot : shots)
    {
        if (shot.shotId == kInvalidShotId ||
            shot.damage <= 0)
        {
            continue;
        }

        for (Enemy &enemy : enemies)
        {
            // Enemy 可能已被同一帧内更早的 Projectile 击杀。
            // 在统一删除前，它仍暂时留在容器中，因此必须跳过。
            if (enemy.isDead())
            {
                continue;
            }

            if (!isCollision(
                    shot.bounds,
                    enemy.bounds()))
            {
                continue;
            }

            // One resolved shot can hit at most one enemy in the Alpha V0
            // adapter. The caller consumes the matching transient flight.
            result.consumedShotIds.push_back(
                shot.shotId);

            const Vec2 hitPosition{
                shot.bounds.position.x +
                    shot.bounds.size.x / 2.0F,
                shot.bounds.position.y +
                    shot.bounds.size.y / 2.0F};

            const HitRegion region = hitRegionAt(
                enemy.bounds(),
                hitPosition);
            const CombatDamageResolution damage = resolveCombatDamage(
                CombatDamageCommand{
                    shot.damage,
                    region,
                    0,
                    0,
                    false,
                    std::nullopt});
            if (!damage.resolved())
            {
                continue;
            }

            const bool killed =
                enemy.takeDamage(damage.damageApplied);

            result.hits.push_back(
                HitResult{
                    shot.shotId,
                    HitTargetKind::Enemy,
                    hitPosition,
                    damage.damageApplied,
                    killed,
                    damage.region,
                    damage.semantic});

            if (killed)
            {
                ++result.enemiesKilled;
            }

            break;
        }
    }

    std::erase_if(
        enemies,
        [](const Enemy &enemy)
        {
            return enemy.isDead();
        });

    return result;
}
