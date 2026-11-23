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

bool shotSegmentIntersectsBounds(
    Vec2 start, Vec2 end, const Rect &bounds, float extent) noexcept
{
    return firstIntersectionFraction(start, end, bounds, extent).has_value();
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
    EnemyLifecycle &enemies)
{
    return resolveShotHits(shots, enemies, {});
}

HitResolutionResult resolveShotHits(
    const std::vector<ShotCollisionCandidate> &shots,
    EnemyLifecycle &enemies,
    const std::vector<BallisticBlocker> &blockers, std::optional<Rect> hostilePlayerTarget,
    std::span<const BallisticBlocker> environmentTargets)
{
    HitResolutionResult result{};

    for (const ShotCollisionCandidate &shot : shots)
    {
        if (shot.shotId == kInvalidShotId ||
            shot.damage <= 0 ||
            shot.penetration < 0 ||
            !validUnarmoredDamageBonus(shot.unarmoredDamageBonus) ||
            std::find(result.consumedShotIds.begin(), result.consumedShotIds.end(), shot.shotId) != result.consumedShotIds.end())
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
        const BallisticBlocker *hitObstacle{};
        BallisticBlockerId environmentId{};
        float targetFraction = std::numeric_limits<float>::infinity();

        // Blockers are considered first and therefore win an exact tie. This
        // prevents an enemy whose bounds touch a wall from being hit through it.
        const auto considerObstacles = [&](std::span<const BallisticBlocker> objects, bool environmental) {
            for (const auto &object : objects)
            {
                const auto fraction = firstIntersectionFraction(shot.start, shot.end, object.bounds, shot.collisionExtent);
                if (!fraction || *fraction >= targetFraction) continue;
                targetFraction = *fraction;
                hitObstacle = &object;
                environmentId = environmental ? object.id : 0;
            }
        };
        considerObstacles(blockers, false);
        considerObstacles(environmentTargets, true);

        bool hitsPlayer = false;
        if (hostilePlayerTarget)
        {
            const auto fraction = firstIntersectionFraction(
                shot.start, shot.end, *hostilePlayerTarget, shot.collisionExtent);
            if (fraction && *fraction < targetFraction)
            {
                targetFraction = *fraction;
                hitObstacle = nullptr;
                hitsPlayer = true;
            }
        }
        for (std::size_t index = 0; index < enemies.size(); ++index)
        {
            const Enemy &enemy = enemies[index];
            if (enemy.isDead() || (hostilePlayerTarget && enemy.isArmedScavenger()))
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

            hitsPlayer = false;
            enemyIndex = index;
            hitObstacle = nullptr;
            targetFraction = *fraction;
        }

        if (!hitsPlayer && !enemyIndex.has_value() && !hitObstacle)
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
        if (hitsPlayer)
        {
            const auto &bounds = *hostilePlayerTarget;
            const Vec2 point{
                std::clamp(collisionCenter.x, bounds.position.x, bounds.position.x + bounds.size.x),
                std::clamp(collisionCenter.y, bounds.position.y, bounds.position.y + bounds.size.y)};
            const auto region = hitRegionAtPoint(bounds, point);
            result.playerImpact = region == HitRegion::Legs ? HitRegion::Legs : HitRegion::Torso;
            continue;
        }
        if (hitObstacle)
        {
            result.hits.push_back(HitResult{
                shot.shotId,
                HitTargetKind::Obstacle,
                collisionCenter,
                0,
                false,
                HitRegion::Torso,
                HitSemantic::Normal});
            result.hits.back().environmentObjectId = environmentId;
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
        // The effect belongs to the bullet and tests the physical contact
        // against the muzzle and range frozen when this shot was fired.
        const int unarmoredBonus = shot.unarmoredDamageBonus > 0 &&
            std::isfinite(shot.unarmoredDamageRange) && shot.unarmoredDamageRange > 0.0F &&
            std::hypot(hitPosition.x - shot.shotOrigin.x, hitPosition.y - shot.shotOrigin.y)
                <= shot.unarmoredDamageRange
            ? shot.unarmoredDamageBonus : 0;
        const CombatDamageResolution damage = resolveCombatDamage(
            CombatDamageCommand{
                shot.damage,
                region,
                shot.penetration,
                shot.damage,
                weakPoint,
                physicalRegion == HitRegion::Torso && enemy.torsoArmor()
                    ? std::optional{enemy.torsoArmor()->protection()} : std::nullopt,
                unarmoredBonus});
        if (!damage.resolved())
        {
            continue;
        }

        enemy.applyArmorLoss(damage.armorDurabilityLoss);
        const bool killed = enemy.takeDamage(damage.damageApplied);

        result.hits.push_back(
            HitResult{
                shot.shotId,
                HitTargetKind::Enemy,
                hitPosition,
                damage.damageApplied,
                killed,
                damage.region,
                damage.semantic,
                enemy.combatTargetId(),
                damage.armorReducedDamage,
                damage.armorDurabilityLoss > 0 && enemy.torsoArmor()->durability == 0});

        if (killed)
        {
            ++result.enemiesKilled;
        }
    }

    result.removals = enemies.removeDead();

    return result;
}
