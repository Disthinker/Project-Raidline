#include "gameplay_world.h"
#include "content_registry.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace
{
constexpr float kEngagementRange = 400.0F;
constexpr float kAimSeconds = 0.8F;
constexpr float kReloadSeconds = 2.5F;
constexpr float kShotExtent = 8.0F;

Vec2 centerOf(Rect bounds) noexcept
{
    return {bounds.position.x + bounds.size.x * 0.5F, bounds.position.y + bounds.size.y * 0.5F};
}
}

void GameplayWorld::addArmedScavenger(const EnemySpawn &spawn, CombatTargetId id)
{
    const auto &config = *spawn.armedScavenger;
    if (spawn.sourceEnemyId.empty() || spawn.variant || spawn.lostWeaponCarrier ||
        config.loadedRounds < 0 || config.loadedRounds > 6 ||
        config.spareRounds < 0 || config.spareRounds > 6 ||
        (config.spareMagazineInstalled && config.spareRounds != 0) ||
        (config.retreatPoint && (!std::isfinite(config.retreatPoint->x) || !std::isfinite(config.retreatPoint->y))) ||
        config.pistol.damage <= 0 || config.pistol.penetration < 0 ||
        !std::isfinite(config.pistol.projectileSpeed) || config.pistol.projectileSpeed <= 0 ||
        !std::isfinite(config.pistol.noiseRadius) || config.pistol.noiseRadius < 0 ||
        std::ranges::any_of(scavengerViews_, [&](const auto &other) {
            return other.sourceEnemyId == spawn.sourceEnemyId;
        }))
        throw std::invalid_argument{"Invalid armed scavenger spawn"};

    scavengerViews_.push_back({spawn.sourceEnemyId, id, config.loadedRounds, config.spareRounds,
        config.spareMagazineInstalled, 12 - config.loadedRounds - config.spareRounds});
    scavengerWeapons_.push_back({config.pistol, centerOf({spawn.position, spawn.size})});
    scavengerWeapons_.back().retreatPoint = config.retreatPoint;
}

void GameplayWorld::checkScavengerRetreat(std::size_t index, const Enemy &actor)
{
    auto &view = scavengerViews_[index];
    auto &weapon = scavengerWeapons_[index];
    if (!weapon.retreatPoint || view.retreating || actor.isDead() ||
        (actor.health() > actor.maxHealth() / 3 && (view.loadedRounds > 0 || view.spareRounds > 0)))
        return;
    view.retreating = true;
    view.aiming = view.reloading = false;
    view.aimDirection = {};
    weapon.target.reset();
    weapon.elapsed = 0;
}

bool GameplayWorld::confirmArmedScavengerEscape(std::string_view sourceId)
{
    if (activeInteriorIndex_) return false;
    for (std::size_t i = 0; i < scavengerViews_.size(); ++i)
    {
        auto &view = scavengerViews_[i];
        if (view.sourceEnemyId != sourceId || !view.alive || !view.escapeReady) continue;
        const auto *actor = enemies_.find(view.id);
        if (!actor) return false;
        const auto &goal = scavengerWeapons_[i].retreatPoint;
        const Vec2 center = centerOf(actor->bounds());
        if (!goal || std::hypot(goal->x - center.x, goal->y - center.y) > kRaidScavengerRetreatExitRadius)
            return false;
        const auto removed = enemies_.removeForObjective(std::span<const CombatTargetId>{&view.id, 1});
        if (removed.empty()) return false;
        consumeEnemyRemovals(removed);
        enemyObservationCache_.clear();
        enemyObservationIndexReady_ = false;
        return true;
    }
    return false;
}

bool GameplayWorld::scavengerSight(Vec2 start, Vec2 end, ScavengerSightSample &sample)
{
    if (sample.visible && sample.start.x == start.x && sample.start.y == start.y &&
        sample.end.x == end.x && sample.end.y == end.y)
        return *sample.visible;
    std::size_t checks{};
    const bool visible = !activeSmokeSight_.blocks(start, end) && outdoorBlockerIndex_->hasLineOfSight(start, end, &checks);
    simulationWorkloadLastUpdate_.lineOfSightBlockersExamined += checks;
    sample = {start, end, visible};
    return visible;
}

GameplayWorld::InfectedGuardTarget GameplayWorld::selectInfectedGuardTarget(
    const Enemy &infected, Vec2 position, Vec2 player, std::span<Enemy *const> guards,
    ScavengerSightSample *sight)
{
    InfectedGuardTarget choice;
    if (infected.isArmedScavenger() || infected.isDead()) return choice;
    if (infected.attackPhase() != EnemyAttackPhase::Idle)
    {
        for (std::size_t i{}; i < guards.size(); ++i)
        {
            const auto &locks = scavengerWeapons_[i].attackLocks;
            if (std::ranges::find(locks, infected.combatTargetId()) != locks.end())
            {
                choice.guard = static_cast<int>(i);
                break;
            }
        }
        return choice;
    }
    const auto squaredDistance = [position](Vec2 to) {
        const float x = position.x - to.x, y = position.y - to.y;
        return x * x + y * y;
    };
    float best = infected.acquireTargetDistance() * infected.acquireTargetDistance();
    const float playerDistance = squaredDistance(player);
    for (std::size_t i{}; i < guards.size(); ++i)
    {
        if (!guards[i] || guards[i]->isDead()) continue;
        const Vec2 post = scavengerWeapons_[i].guardCenter;
        const float distance = squaredDistance(post);
        if (distance > best) continue;
        if (distance >= playerDistance)
        {
            if (!choice.playerVisible) choice.playerVisible = scavengerSight(position, player, sight[0]);
            if (*choice.playerVisible) continue;
        }
        if (!scavengerSight(position, post, sight[i + 1])) continue;
        best = distance;
        choice.guard = static_cast<int>(i);
    }
    return choice;
}

void GameplayWorld::clearScavengerAim() noexcept
{
    scavengerShotEvents_.clear();
    for (std::size_t i{}; i < scavengerViews_.size(); ++i)
    {
        auto &view = scavengerViews_[i];
        auto &weapon = scavengerWeapons_[i];
        view.aiming = false;
        view.aimDirection = {};
        weapon.target.reset();
        weapon.flight.reset();
        if (!view.reloading) weapon.elapsed = 0.0F;
    }
}

void GameplayWorld::updateScavengerWeapons(float seconds)
{
    if (!std::isfinite(seconds) || seconds <= 0.0F || activeInteriorIndex_) return;
    const Rect playerBounds{player_.position(), {player_.size(), player_.size()}};
    for (std::size_t i{}; i < scavengerViews_.size(); ++i)
    {
        auto &view = scavengerViews_[i];
        auto &weapon = scavengerWeapons_[i];
        if (weapon.flight)
        {
            auto &flight = *weapon.flight;
            const auto segment = flight.advance(seconds);
            const auto result = resolveShotHits({{flight.shotId(), segment.start, segment.end,
                flight.collisionExtent(), flight.damage(), std::nullopt, flight.penetration(), flight.origin()}},
                enemies_, ballisticBlockers_, playerBounds);
            if (result.playerImpact)
            {
                const PlayerDamageObservation damage{flight.damage(), *result.playerImpact,
                    flight.penetration(), flight.damage(), false, WoundSource::None, view.id, std::nullopt};
                if (deferPlayerDamageResolution_)
                    pendingPlayerDamageObservations_.push_back(damage);
                else if (damagePlayer(damage.baseDamage))
                    return;
            }
            if (segment.reachedImpact || !result.consumedShotIds.empty()) weapon.flight.reset();
            consumeEnemyRemovals(result.removals);
        }
        if (!view.alive) continue;
        if (weapon.retreatPoint)
        {
            // Player fire and infected contact can wound a guard after the last movement substep.
            if (!view.retreating)
                if (const auto *actor = enemies_.find(view.id)) checkScavengerRetreat(i, *actor);
            if (view.retreating) continue;
        }

        if (view.loadedRounds == 0)
        {
            if (view.spareRounds == 0) continue;
            view.reloading = true;
            weapon.elapsed += seconds;
            if (weapon.elapsed >= kReloadSeconds)
            {
                view.loadedRounds = view.spareRounds;
                view.spareRounds = 0;
                view.spareMagazineInstalled = true;
                view.ammunitionChangedLastUpdate = true;
                view.reloading = false;
                weapon.elapsed = 0.0F;
            }
            continue;
        }

        const Vec2 origin = weapon.guardCenter;
        float bestDistance = kEngagementRange * kEngagementRange;
        std::optional<CombatTargetId> target;
        Vec2 aim{};
        const auto consider = [&](CombatTargetId id, Rect bounds)
        {
            const Vec2 destination = centerOf(bounds);
            const Vec2 delta{destination.x - origin.x, destination.y - origin.y};
            const float distance = delta.x * delta.x + delta.y * delta.y;
            if (distance < 1e-6F || distance > bestDistance ||
                (distance == bestDistance && target && *target < id) ||
                activeSmokeSight_.blocks(origin, destination) || !outdoorBlockerIndex_->hasLineOfSight(origin, destination))
                return;
            bestDistance = distance;
            target = id;
            aim = delta;
        };
        if (!player_.isDead()) consider(kInvalidCombatTargetId, playerBounds);
        for (const auto &enemy : enemies_)
            if (!enemy.isDead() && !enemy.isArmedScavenger())
                consider(enemy.combatTargetId(), enemy.bounds());

        const bool newTarget = target != weapon.target;
        if (!target || newTarget)
        {
            weapon.elapsed = 0.0F;
            weapon.target = target;
        }
        view.aiming = target.has_value();
        if (!target)
        {
            view.aimDirection = {};
            continue;
        }
        const float length = std::sqrt(bestDistance);
        view.aimDirection = {aim.x / length, aim.y / length};
        if (newTarget) continue; // Present the warning for a frame even after a long step.
        weapon.elapsed += seconds;
        if (weapon.elapsed < kAimSeconds || weapon.flight) continue;
        const auto shot = resolveShotCommand({nextScavengerShotId_, origin, view.aimDirection,
            weapon.pistol.projectileSpeed, kShotExtent, weapon.pistol.damage, kEngagementRange,
            std::nullopt, weapon.pistol.penetration});
        if (!shot.accepted()) continue;

        weapon.flight.emplace(shot);
        ++nextScavengerShotId_;
        ++view.shotsFired;
        view.ammunitionChangedLastUpdate = true;
        --view.loadedRounds;
        view.aiming = false;
        view.reloading = view.loadedRounds == 0 && view.spareRounds > 0;
        weapon.elapsed = 0.0F;
        weapon.target.reset();
        scavengerShotEvents_.push_back({origin, shot.direction, weapon.pistol.noiseRadius});
        emitRaidNoise(origin, weapon.pistol.noiseRadius);
    }
}
