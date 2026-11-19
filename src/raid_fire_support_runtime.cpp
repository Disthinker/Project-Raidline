#include "raid_fire_support_runtime.h"

#include "combat_damage_domain.h"
#include "enemy.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace
{
    constexpr float kFollowDistance = 57.0F;
    constexpr float kFollowSpeed = 285.0F;
    constexpr float kCatchUpDistance = 170.0F;
    constexpr float kCatchUpSpeed = 375.0F;
    constexpr float kNavigationRefreshSeconds = 0.20F;
    constexpr float kShotExtent = 8.0F;
    constexpr float kMovementEpsilon = 0.001F;

    float distanceSquared(Vec2 left, Vec2 right) noexcept
    {
        const float x = left.x - right.x;
        const float y = left.y - right.y;
        return x * x + y * y;
    }

    float clippedShotDistance(Vec2 origin, Vec2 direction, Vec2 size) noexcept
    {
        float range = kFireSupportRange;
        if (direction.x != 0.0F)
        {
            const float boundary = direction.x > 0.0F ? size.x : 0.0F;
            range = std::min(range, (boundary - origin.x) / direction.x);
        }
        if (direction.y != 0.0F)
        {
            const float boundary = direction.y > 0.0F ? size.y : 0.0F;
            range = std::min(range, (boundary - origin.y) / direction.y);
        }
        return range;
    }
}

RaidFireSupportRuntime::RaidFireSupportRuntime(FireSupportConfig config, Vec2 spawnPosition)
    : config_{config}, position_{spawnPosition}
{
    if (config.damage <= 0 || config.penetration < 0 ||
        !std::isfinite(config.projectileSpeed) || config.projectileSpeed <= 0.0F ||
        !std::isfinite(config.noiseRadius) || config.noiseRadius <= 0.0F)
    {
        throw std::invalid_argument{"Invalid fire support rifle configuration"};
    }
}

Vec2 RaidFireSupportRuntime::center() const noexcept
{
    return {position_.x + kSize.x * 0.5F, position_.y + kSize.y * 0.5F};
}

FireSupportView RaidFireSupportRuntime::view() const noexcept
{
    return {bounds(), health_.current(), movementOrder_, fireOrder_};
}

void RaidFireSupportRuntime::setOrders(
    FireSupportMovementOrder movement, FireSupportFireOrder fire) noexcept
{
    if (!alive())
        return;
    if (movementOrder_ != movement)
        clearNavigation();
    movementOrder_ = movement;
    fireOrder_ = fire;
}

void RaidFireSupportRuntime::clearNavigation() noexcept
{
    waypoint_.reset();
    navigationRefresh_ = 0.0F;
}

void RaidFireSupportRuntime::clearSpatialTransient() noexcept
{
    clearNavigation();
    flight_.reset();
    shotEvent_.reset();
}

void RaidFireSupportRuntime::leaveSpace() noexcept
{
    clearSpatialTransient();
    attackLocks_.clear();
}

Vec2 RaidFireSupportRuntime::prepareMovement(
    Vec2 goal, const RaidSpaceNavigationField *field,
    const RaidSpaceBlockerIndex &blockers, float seconds)
{
    if (!alive() || !std::isfinite(seconds) || seconds <= 0.0F)
        return position_;
    contactProtection_ = std::max(0.0F, contactProtection_ - seconds);
    controlRemaining_ = std::max(0.0F, controlRemaining_ - seconds);
    if (controlRemaining_ > 0.0F || movementOrder_ == FireSupportMovementOrder::Hold)
        return position_;

    const Vec2 start = center();
    const float goalDistanceSquared = distanceSquared(start, goal);
    const bool close = goalDistanceSquared <= kFollowDistance * kFollowDistance;
    if (close && blockers.hasLineOfSight(start, goal))
    {
        clearNavigation();
        return position_;
    }

    navigationRefresh_ -= seconds;
    if (navigationRefresh_ <= 0.0F)
    {
        // The navigation field works with actor centers and expands for the
        // full footprint. Clamp the goal if the player stands at a map edge.
        const Vec2 world = blockers.worldSize();
        const Vec2 half{kSize.x * 0.5F, kSize.y * 0.5F};
        goal.x = std::clamp(goal.x, half.x, world.x - half.x);
        goal.y = std::clamp(goal.y, half.y, world.y - half.y);
        waypoint_ = field ? field->nextWaypoint(start, goal, 8.0F) : std::nullopt;
        navigationRefresh_ = kNavigationRefreshSeconds;
    }

    if (!waypoint_)
        return position_;
    const Vec2 offset{waypoint_->x - start.x, waypoint_->y - start.y};
    const float distance = std::hypot(offset.x, offset.y);
    if (distance <= kMovementEpsilon)
        return position_;
    const float speed = goalDistanceSquared > kCatchUpDistance * kCatchUpDistance
        ? kCatchUpSpeed : kFollowSpeed;
    const float movement = std::min(distance, speed * seconds) / distance;
    return {position_.x + offset.x * movement, position_.y + offset.y * movement};
}

bool RaidFireSupportRuntime::hasAttackLock(CombatTargetId id) const noexcept
{
    return std::ranges::find(attackLocks_, id) != attackLocks_.end();
}

void RaidFireSupportRuntime::lockAttack(CombatTargetId id)
{
    attackLocks_.push_back(id);
}

void RaidFireSupportRuntime::forgetAttack(CombatTargetId id) noexcept
{
    std::erase(attackLocks_, id);
}

void RaidFireSupportRuntime::applyContact(
    const PlayerDamageObservation &damage, float controlDuration)
{
    const auto outcome = resolveCombatDamage({
        damage.baseDamage, damage.region, damage.penetration, damage.armorDamage,
        damage.weakPoint, std::nullopt});
    if (outcome.resolved() && outcome.damageApplied > 0)
    {
        static_cast<void>(health_.takeDamage(outcome.damageApplied));
        controlRemaining_ = std::max(controlRemaining_, controlDuration);
    }
}

void RaidFireSupportRuntime::tryFire(
    Rect protectedPlayer, const EnemyLifecycle &targets,
    const RaidSpaceBlockerIndex &blockerIndex)
{
    const Vec2 origin = center();
    float closestDistanceSquared = kFireSupportRange * kFireSupportRange;
    std::optional<ShotResolution> selected;
    for (std::size_t i{}; i < targets.size(); ++i)
    {
        const Enemy &target = targets[i];
        if (target.isDead())
            continue;
        const Rect targetBounds = target.bounds();
        const Vec2 destination{
            targetBounds.position.x + targetBounds.size.x * 0.5F,
            targetBounds.position.y + targetBounds.size.y * 0.5F};
        const float distance = distanceSquared(origin, destination);
        if (distance <= kMovementEpsilon * kMovementEpsilon ||
            distance > closestDistanceSquared ||
            (selected && distance == closestDistanceSquared) ||
            !blockerIndex.hasLineOfSight(origin, destination))
        {
            continue;
        }
        const float inverseDistance = 1.0F / std::sqrt(distance);
        const Vec2 direction{
            (destination.x - origin.x) * inverseDistance,
            (destination.y - origin.y) * inverseDistance};
        const float maximumDistance = clippedShotDistance(
            origin, direction, blockerIndex.worldSize());
        const auto shot = resolveShotCommand({
            nextShotId_, origin, direction, config_.projectileSpeed, kShotExtent,
            config_.damage, maximumDistance, std::nullopt, config_.penetration});
        if (!shot.accepted() ||
            shotSegmentIntersectsBounds(origin, shot.impactPosition, protectedPlayer, kShotExtent))
        {
            continue;
        }
        closestDistanceSquared = distance;
        selected = shot;
    }
    if (selected)
    {
        flight_.emplace(*selected);
        shotEvent_ = FireSupportShotEvent{origin, selected->direction, config_.noiseRadius};
        shotCooldown_ = kFireSupportShotInterval;
        ++nextShotId_;
    }
}

HitResolutionResult RaidFireSupportRuntime::updateWeapon(
    float seconds, Rect protectedPlayer, EnemyLifecycle &targets,
    const RaidSpaceBlockerIndex &blockerIndex,
    const std::vector<BallisticBlocker> &blockers)
{
    if (!std::isfinite(seconds) || seconds <= 0.0F)
        return {};
    shotCooldown_ = std::max(0.0F, shotCooldown_ - seconds);
    if (alive() && controlRemaining_ == 0.0F && fireOrder_ == FireSupportFireOrder::FreeFire &&
        shotCooldown_ == 0.0F && !flight_)
    {
        tryFire(protectedPlayer, targets, blockerIndex);
    }
    if (!flight_)
        return {};

    const auto segment = flight_->advance(seconds);
    if (shotSegmentIntersectsBounds(segment.start, segment.end, protectedPlayer, kShotExtent))
    {
        flight_.reset();
        return {};
    }
    const LogicalBallisticFlight &flight = *flight_;
    auto result = resolveShotHits(
        {ShotCollisionCandidate{
            flight.shotId(), segment.start, segment.end, flight.collisionExtent(),
            flight.damage(), flight.aimIntent(), flight.penetration(), flight.origin()}},
        targets, blockers);
    if (segment.reachedImpact || !result.consumedShotIds.empty())
        flight_.reset();
    return result;
}
