#include "hit_resolution.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <optional>
#include <vector>

namespace
{
    bool finite(Vec2 value) noexcept
    {
        return std::isfinite(value.x) && std::isfinite(value.y);
    }

    std::optional<float> firstIntersectionFraction(
        Vec2 start,
        Vec2 end,
        const Rect &target,
        float collisionExtent) noexcept
    {
        const float halfExtent = collisionExtent / 2.0F;
        const Vec2 minimum{
            target.position.x - halfExtent,
            target.position.y - halfExtent};
        const Vec2 maximum{
            target.position.x + target.size.x + halfExtent,
            target.position.y + target.size.y + halfExtent};
        const Vec2 delta{end.x - start.x, end.y - start.y};

        float enter = 0.0F;
        float leave = 1.0F;
        auto clipAxis = [&](float origin, float movement,
                            float lower, float upper)
        {
            constexpr float epsilon{0.000001F};
            if (std::abs(movement) <= epsilon)
            {
                return origin >= lower && origin <= upper;
            }

            float first = (lower - origin) / movement;
            float second = (upper - origin) / movement;
            if (first > second)
            {
                std::swap(first, second);
            }
            enter = std::max(enter, first);
            leave = std::min(leave, second);
            return enter <= leave;
        };

        if (!clipAxis(start.x, delta.x, minimum.x, maximum.x) ||
            !clipAxis(start.y, delta.y, minimum.y, maximum.y) ||
            leave < 0.0F || enter > 1.0F)
        {
            return std::nullopt;
        }
        return std::clamp(enter, 0.0F, 1.0F);
    }

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

        if (!finite(shot.start) || !finite(shot.end) ||
            !std::isfinite(shot.collisionExtent) ||
            shot.collisionExtent <= 0.0F)
        {
            continue;
        }

        std::optional<std::size_t> targetIndex;
        float targetFraction = std::numeric_limits<float>::infinity();
        for (std::size_t index = 0; index < enemies.size(); ++index)
        {
            const Enemy &enemy = enemies[index];
            if (enemy.isDead())
            {
                continue;
            }

            const std::optional<float> fraction = firstIntersectionFraction(
                shot.start,
                shot.end,
                enemy.bounds(),
                shot.collisionExtent);
            if (!fraction.has_value() || *fraction >= targetFraction)
            {
                continue;
            }

            targetIndex = index;
            targetFraction = *fraction;
        }

        if (!targetIndex.has_value())
        {
            continue;
        }

        Enemy &enemy = enemies[*targetIndex];

        const Vec2 sweepDelta{
            shot.end.x - shot.start.x,
            shot.end.y - shot.start.y};
        const Vec2 collisionCenter{
            shot.start.x + sweepDelta.x * targetFraction,
            shot.start.y + sweepDelta.y * targetFraction};
        const Rect enemyBounds = enemy.bounds();
        const Vec2 hitPosition{
            std::clamp(
                collisionCenter.x,
                enemyBounds.position.x,
                enemyBounds.position.x + enemyBounds.size.x),
            std::clamp(
                collisionCenter.y,
                enemyBounds.position.y,
                enemyBounds.position.y + enemyBounds.size.y)};

        const HitRegion region = hitRegionAt(enemyBounds, hitPosition);
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

        result.consumedShotIds.push_back(shot.shotId);
        const bool killed = enemy.takeDamage(damage.damageApplied);

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
    }

    std::erase_if(
        enemies,
        [](const Enemy &enemy)
        {
            return enemy.isDead();
        });

    return result;
}
