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

}

std::optional<HitRegion> hitRegionAtPoint(
    const Rect &target,
    Vec2 point) noexcept
{
    if (!finite(target.position) || !finite(target.size) ||
        !finite(point) || target.size.x <= 0.0F || target.size.y <= 0.0F ||
        point.x < target.position.x ||
        point.x > target.position.x + target.size.x ||
        point.y < target.position.y ||
        point.y > target.position.y + target.size.y)
    {
        return std::nullopt;
    }

    const float relativeY =
        (point.y - target.position.y) / target.size.y;
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

HitResolutionResult resolveShotEnemyHits(
    const std::vector<ShotCollisionCandidate> &shots,
    std::vector<Enemy> &enemies)
{
    return resolveShotHits(shots, enemies, {});
}

HitResolutionResult resolveShotHits(
    const std::vector<ShotCollisionCandidate> &shots,
    std::vector<Enemy> &enemies,
    const std::vector<BallisticBlocker> &blockers)
{
    HitResolutionResult result{};

    for (const ShotCollisionCandidate &shot : shots)
    {
        if (shot.shotId == kInvalidShotId ||
            shot.damage <= 0 ||
            shot.penetration < 0)
        {
            continue;
        }

        if (!finite(shot.start) || !finite(shot.end) ||
            !std::isfinite(shot.collisionExtent) ||
            shot.collisionExtent <= 0.0F)
        {
            continue;
        }

        std::optional<std::size_t> enemyIndex;
        std::optional<std::size_t> blockerIndex;
        float targetFraction = std::numeric_limits<float>::infinity();

        // Blockers are considered first and therefore win an exact tie. This
        // prevents an enemy whose bounds touch a wall from being hit through it.
        for (std::size_t index = 0; index < blockers.size(); ++index)
        {
            const std::optional<float> fraction = firstIntersectionFraction(
                shot.start,
                shot.end,
                blockers[index].bounds,
                shot.collisionExtent);
            if (!fraction.has_value() || *fraction >= targetFraction)
            {
                continue;
            }
            blockerIndex = index;
            enemyIndex.reset();
            targetFraction = *fraction;
        }

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

            enemyIndex = index;
            blockerIndex.reset();
            targetFraction = *fraction;
        }

        if (!enemyIndex.has_value() && !blockerIndex.has_value())
        {
            continue;
        }

        const Vec2 sweepDelta{
            shot.end.x - shot.start.x,
            shot.end.y - shot.start.y};
        const Vec2 collisionCenter{
            shot.start.x + sweepDelta.x * targetFraction,
            shot.start.y + sweepDelta.y * targetFraction};

        result.consumedShotIds.push_back(shot.shotId);
        if (blockerIndex.has_value())
        {
            result.hits.push_back(HitResult{
                shot.shotId,
                HitTargetKind::Obstacle,
                collisionCenter,
                0,
                false,
                HitRegion::Torso,
                HitSemantic::Normal});
            continue;
        }

        Enemy &enemy = enemies[*enemyIndex];
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

        const HitRegion physicalRegion =
            hitRegionAtPoint(enemyBounds, hitPosition)
                .value_or(HitRegion::Torso);
        const bool aimMatchesPhysicalRegion =
            shot.aimIntent.has_value() &&
            shot.aimIntent->targetId == enemy.combatTargetId() &&
            shot.aimIntent->region == physicalRegion;

        // Headshots and weak points are precision semantics, not a side
        // effect of an infinite ray crossing the right Y coordinate. They
        // require both a matching fire-time reticle intent and a physical
        // ballistic impact on the same target/region. A geometrical head
        // contact without that intent remains an ordinary torso-equivalent
        // hit; torso/leg impacts retain their actual region.
        const HitRegion region =
            physicalRegion == HitRegion::Head && !aimMatchesPhysicalRegion
                ? HitRegion::Torso
                : physicalRegion;
        const bool weakPoint =
            aimMatchesPhysicalRegion && shot.aimIntent->weakPoint;
        const CombatDamageResolution damage = resolveCombatDamage(
            CombatDamageCommand{
                shot.damage,
                region,
                0,
                shot.penetration,
                weakPoint,
                std::nullopt});
        if (!damage.resolved())
        {
            continue;
        }

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

    // Prune every dead object, including enemies that were marked dead by a
    // scenario/test command before this resolver ran. Reporting the complete
    // sorted index set lets the owning world prune all parallel per-enemy
    // runtime state with the exact same shape change.
    for (std::size_t index{}; index < enemies.size(); ++index)
    {
        if (enemies[index].isDead())
        {
            result.removedEnemyIndices.push_back(index);
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
