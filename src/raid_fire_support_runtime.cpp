#include "raid_fire_support_runtime.h"

#include "combat_damage_domain.h"
#include "enemy.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace
{
    constexpr float kCatchUpDistance = 170.0F;
    constexpr float kNavigationRefreshSeconds = 0.20F;
    constexpr float kShotExtent = 8.0F;
    constexpr float kMovementEpsilon = 0.001F;

    float distanceSquared(Vec2 left, Vec2 right) noexcept
    {
        const float x = left.x - right.x;
        const float y = left.y - right.y;
        return x * x + y * y;
    }

    float clippedShotDistance(Vec2 origin, Vec2 direction, Vec2 size, float range) noexcept
    {
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
    : RaidCompanionRuntime{spawnPosition}, config_{config}
{
    observerPosition_ = center();
    if (config.damage <= 0 || config.penetration < 0 ||
        !std::isfinite(config.projectileSpeed) || config.projectileSpeed <= 0.0F ||
        !std::isfinite(config.noiseRadius) || config.noiseRadius <= 0.0F)
    {
        throw std::invalid_argument{"Invalid fire support rifle configuration"};
    }
}

Vec2 RaidCompanionRuntime::center() const noexcept
{
    return {position_.x + kSize.x * 0.5F, position_.y + kSize.y * 0.5F};
}

FireSupportView RaidFireSupportRuntime::view() const noexcept
{
    FireSupportView result{bounds(), health(), movementOrder_, fireOrder_};
    result.sniperReport = report_;
    result.sniperOutcome = sniperOutcome_;
    result.treating = receivingTreatment_;
    return result;
}

void RaidCompanionRuntime::setMovementOrder(FireSupportMovementOrder movement) noexcept
{
    if (movementOrder_ != movement) clearNavigation();
    movementOrder_ = movement;
}

void RaidFireSupportRuntime::setOrders(
    FireSupportMovementOrder movement, FireSupportFireOrder fire) noexcept
{
    if (!alive())
        return;
    setMovementOrder(movement);
    fireOrder_ = fire;
}

void RaidCompanionRuntime::clearNavigation() noexcept
{
    waypoint_.reset();
    navigationRefresh_ = 0.0F;
}

void RaidFireSupportRuntime::clearSpatialTransient() noexcept
{
    clearNavigation();
    report_.reset();
    reportedTarget_.reset();
    authorizedTarget_.reset();
    sniperOutcome_ = SniperShotOutcome::None;
    observerPosition_ = center();
    flight_.reset();
    shotEvent_.reset();
}

void RaidFireSupportRuntime::leaveSpace() noexcept
{
    clearSpatialTransient();
    clearAttackLocks();
}

Vec2 RaidCompanionRuntime::prepareMovement(
    Vec2 goal, const RaidSpaceNavigationField *field,
    const RaidSpaceBlockerIndex &blockers, float seconds, float stopDistance)
{
    if (!alive() || !std::isfinite(seconds) || seconds <= 0.0F)
        return position_;
    contactProtection_ = std::max(0.0F, contactProtection_ - seconds);
    controlRemaining_ = std::max(0.0F, controlRemaining_ - seconds);
    if (receivingTreatment_ || controlRemaining_ > 0.0F || movementOrder_ == FireSupportMovementOrder::Hold)
        return position_;

    const Vec2 start = center();
    const float goalDistanceSquared = distanceSquared(start, goal);
    const bool close = goalDistanceSquared <= stopDistance * stopDistance;
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
        ? catchUpSpeed_ : followSpeed_;
    const float movement = std::min(distance, speed * seconds) / distance;
    return {position_.x + offset.x * movement, position_.y + offset.y * movement};
}

bool RaidCompanionRuntime::hasAttackLock(CombatTargetId id) const noexcept
{
    return std::ranges::find(attackLocks_, id) != attackLocks_.end();
}

void RaidCompanionRuntime::lockAttack(CombatTargetId id)
{
    attackLocks_.push_back(id);
}

void RaidCompanionRuntime::forgetAttack(CombatTargetId id) noexcept
{
    std::erase(attackLocks_, id);
}

void RaidCompanionRuntime::applyContact(
    const PlayerDamageObservation &damage, float controlDuration)
{
    const auto outcome = resolveCombatDamage({
        damage.baseDamage, damage.region, damage.penetration, damage.armorDamage,
        damage.weakPoint, std::nullopt});
    if (outcome.resolved() && outcome.damageApplied > 0)
    {
        static_cast<void>(health_.takeDamage(outcome.damageApplied));
        controlRemaining_ = std::max(controlRemaining_, controlDuration);
        if (receivingTreatment_ && (performingTreatment_ || controlDuration > 0.0F))
            treatmentInterrupted_ = true;
    }
}

void RaidFireSupportRuntime::observeSniperTargets(
    const EnemyLifecycle &targets, const RaidSpaceBlockerIndex &blockers, const RaidSmokeSight &smoke,
    RaidSightParameters ambient, float seconds, std::span<const RaidIlluminationCoverage *const> lights)
{
    const Vec2 origin = center();
    const float dx = origin.x - observerPosition_.x, dy = origin.y - observerPosition_.y;
    if (dx * dx + dy * dy > .01F) watchHeading_ = std::atan2(dy, dx);
    else if (!reportedTarget_ && std::isfinite(seconds))
        watchHeading_ = std::remainder(watchHeading_ + std::clamp(seconds, 0.F, 1.F) * .75F, 6.2831853F);
    observerPosition_ = origin;
    const Vec2 facing{std::cos(watchHeading_), std::sin(watchHeading_)};
    const float coneCosine = std::cos(ambient.angleDegrees * .00872664626F);
    const float ambientSquared = ambient.maximumDistance * ambient.maximumDistance;
    sniperRange_ = lights.empty() ? ambient.maximumDistance : raidSightParameters(RaidLightEnvironment::DayOutdoor).maximumDistance;
    float nearest = sniperRange_ * sniperRange_;
    Vec2 reportPoint{};
    std::optional<CombatTargetId> selected;
    report_.reset();
    if (alive()) for (const Enemy &enemy : targets)
    {
        if (enemy.isDead()) continue;
        const Rect box = enemy.bounds();
        const Vec2 point{box.position.x + box.size.x * .5F, box.position.y + box.size.y * .5F};
        const float distance = distanceSquared(origin, point);
        if (distance <= .001F || distance > nearest || (selected && distance == nearest) ||
            (point.x - origin.x) * facing.x + (point.y - origin.y) * facing.y < std::sqrt(distance) * coneCosine)
            continue;
        if (distance > ambientSquared && std::ranges::none_of(lights, [&](const RaidIlluminationCoverage *light) {
            return light->queries[0].pointVisible(point) || light->queries[1].pointVisible(point);
        })) continue;
        if (smoke.blocks(origin, point) || !blockers.hasLineOfSight(origin, point)) continue;
        nearest = distance;
        reportPoint = point;
        selected = enemy.combatTargetId();
        auto kind = SniperTargetKind::Infected;
        if (enemy.isArmedScavenger()) kind = SniperTargetKind::Scavenger;
        else if (enemy.torsoArmor()) kind = SniperTargetKind::Armored;
        else if (enemy.variant() && enemy.variant()->kind == EnemyVariantKind::Runner) kind = SniperTargetKind::Runner;
        constexpr float eighthTurn = 0.7853981634F;
        const int direction = static_cast<int>(std::lround(std::atan2(point.y - origin.y, point.x - origin.x) / eighthTurn));
        report_ = SniperReport{kind, static_cast<unsigned>((direction + 8) % 8)};
    }
    if (selected != reportedTarget_)
    {
        authorizedTarget_.reset();
        ++reportGeneration_;
    }
    if (report_)
    {
        report_->token = reportGeneration_;
        watchHeading_ = std::atan2(reportPoint.y - origin.y, reportPoint.x - origin.x);
    }
    reportedTarget_ = selected;
}

bool RaidFireSupportRuntime::authorizeSniperShot(std::uint64_t token) noexcept
{
    if (!alive() || !reportedTarget_ || !report_ || token != report_->token || authorizedTarget_ || flight_) return false;
    authorizedTarget_ = reportedTarget_;
    return true;
}

void RaidFireSupportRuntime::tryFire(
    Rect protectedPlayer, const EnemyLifecycle &targets,
    const RaidSpaceBlockerIndex &blockerIndex, const RaidSmokeSight &smoke, bool sniper)
{
    const Vec2 origin = center();
    const float range = sniper ? sniperRange_ : kFireSupportRange;
    float closestDistanceSquared = range * range;
    std::optional<ShotResolution> selected;
    for (std::size_t i{}; i < targets.size(); ++i)
    {
        const Enemy &target = targets[i];
        if (target.isDead() || (sniper && target.combatTargetId() != authorizedTarget_))
            continue;
        const Rect targetBounds = target.bounds();
        const Vec2 destination{
            targetBounds.position.x + targetBounds.size.x * 0.5F,
            targetBounds.position.y + targetBounds.size.y * 0.5F};
        const float distance = distanceSquared(origin, destination);
        if (distance <= kMovementEpsilon * kMovementEpsilon ||
            distance > closestDistanceSquared ||
            (selected && distance == closestDistanceSquared) ||
            smoke.blocks(origin, destination) || !blockerIndex.hasLineOfSight(origin, destination))
        {
            continue;
        }
        const float inverseDistance = 1.0F / std::sqrt(distance);
        const Vec2 direction{
            (destination.x - origin.x) * inverseDistance,
            (destination.y - origin.y) * inverseDistance};
        const float maximumDistance = clippedShotDistance(
            origin, direction, blockerIndex.worldSize(), range);
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
        if (sniper)
        {
            authorizedTarget_.reset();
            sniperOutcome_ = SniperShotOutcome::Fired;
        }
        flight_.emplace(*selected);
        shotEvent_ = FireSupportShotEvent{origin, selected->direction, config_.noiseRadius};
        shotCooldown_ = kFireSupportShotInterval;
        ++nextShotId_;
    }
}

HitResolutionResult RaidFireSupportRuntime::updateWeapon(
    float seconds, Rect protectedPlayer, EnemyLifecycle &targets,
    const RaidSpaceBlockerIndex &blockerIndex,
    const std::vector<BallisticBlocker> &blockers, const RaidSmokeSight &smoke, bool sniper)
{
    if (!std::isfinite(seconds) || seconds <= 0.0F)
        return {};
    shotCooldown_ = std::max(0.0F, shotCooldown_ - seconds);
    if (alive() && !receivingTreatment_ && controlRemaining_ == 0.0F &&
        (sniper ? authorizedTarget_.has_value() : fireOrder_ == FireSupportFireOrder::FreeFire) &&
        shotCooldown_ == 0.0F && !flight_)
    {
        tryFire(protectedPlayer, targets, blockerIndex, smoke, sniper);
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
    if (sniper)
        for (const auto &hit : result.hits)
            if (hit.damageApplied > 0)
                sniperOutcome_ = hit.targetKilled ? SniperShotOutcome::Killed : SniperShotOutcome::Hit;
    if (segment.reachedImpact || !result.consumedShotIds.empty())
        flight_.reset();
    return result;
}
