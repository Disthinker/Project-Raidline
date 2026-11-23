#include "gameplay_world.h"

#include <algorithm>
#include <cmath>

namespace
{
bool withinFire(Vec2 center, float radius, Vec2 target) noexcept
{
    const float dx = target.x - center.x, dy = target.y - center.y;
    return dx * dx + dy * dy <= radius * radius;
}
}

std::optional<FireZoneLanding> GameplayWorld::previewFireZone(Vec2 aim, const FireZoneUseDefinition &use) const
{
    if (!grenadeEnabled_ || developerRange_ || !raidSession_.isActive() || player_.isDead() ||
        player_.isControlled() || !use.valid() || !fireZones_.canStart() || grenade_.active() ||
        noiseDecoy_.flight() || thrownIllumination_.flight() || smoke_.flight() || melee_.recoverySeconds() > 0)
        return std::nullopt;
    const Vec2 p = player_.position();
    const float half = player_.size() * 0.5F;
    NoiseDecoyUseDefinition ray;
    ray.range = use.maximumRange;
    ray.flightSeconds = use.flightSeconds;
    const auto hit = queryNoiseDecoyLanding({p.x + half, p.y + half}, aim, activeRaidSpaceId(), ray, activeBlockerIndex());
    if (!hit) return std::nullopt;
    return FireZoneLanding{hit->origin, hit->point, hit->space, use};
}

bool GameplayWorld::fireZoneReaches(const RaidFireZone &zone, Vec2 target) const noexcept
{
    return zone.space == activeRaidSpaceId() && withinFire(zone.point, zone.radius, target) &&
        activeBlockerIndex().hasLineOfSight(zone.point, target);
}

std::shared_ptr<const RaidIlluminationCoverage> GameplayWorld::fireZoneCoverage(const RaidFireZone &zone) const
{
    if (zone.space != activeRaidSpaceId()) return {};
    const auto zones = fireZones_.zones();
    for (std::size_t i = 0; i < zones.size(); ++i)
        if (zones[i].space == zone.space && zones[i].point.x == zone.point.x &&
            zones[i].point.y == zone.point.y && zones[i].radius == zone.radius)
            return getRaidIlluminationCoverage(fireCoverage_[i], activeBlockerIndex(), zone.space,
                activeGeometryRevision_, "fire-zone", zone.point, zone.radius);
    return {};
}

void GameplayWorld::advanceFireZones(float seconds, Vec2 before, Vec2 after, bool transitioned)
{
    frameFireIntervalCount_ = 0;
    fireZones_.advance(seconds, [&](const RaidFireZone &zone, double from, double until) {
        if (!transitioned && zone.space == activeRaidSpaceId() && until > from)
            frameFireIntervals_[frameFireIntervalCount_++] = {zone.point, zone.radius, from, until, zone.use.damagePerSecond};
    });
    if (transitioned || (seconds > 0 && !frameFireIntervalCount_))
    {
        playerFireExposure_ = 0;
        ++fireExposureGeneration_;
    }
    firePlayerFrameStart_ = before;
    firePlayerFrameEnd_ = after;
}

void GameplayWorld::collectFireDamage(Vec2 point, CombatTargetId id, double &credit,
    double from, double until, std::size_t firstPulse)
{
    // Enemy movement already uses bounded simulation steps. Sample their
    // actual positions and the player's resolved movement at the same cadence.
    struct Window { double from{}, until{}; int damage{}; };
    std::array<Window, RaidFireZoneRuntime::kCapacity> windows{};
    std::size_t count{};
    for (std::size_t i = 0; i < frameFireIntervalCount_; ++i)
    {
        const auto &zone = frameFireIntervals_[i];
        const double start = std::max(from, zone.from), end = std::min(until, zone.until);
        if (end > start && withinFire(zone.center, zone.radius, point) &&
            activeBlockerIndex().hasLineOfSight(zone.center, point))
            windows[count++] = {start, end, zone.damage};
    }
    if (!count) { credit = 0; return; }
    if (count == 2 && windows[1].from < windows[0].from) std::swap(windows[1], windows[0]);
    if (count == 2 && windows[1].from <= windows[0].until)
    {
        windows[0].until = std::max(windows[0].until, windows[1].until);
        windows[0].damage = std::max(windows[0].damage, windows[1].damage);
        count = 1;
    }
    constexpr double timeTolerance = 1e-7;
    double previous = from;
    std::size_t pulseIndex = firstPulse;
    for (std::size_t i = 0; i < count; ++i)
    {
        const auto window = windows[i];
        if (window.from > previous + timeTolerance) credit = 0;
        credit += window.until - window.from;
        while (credit + timeTolerance >= 1.0)
        {
            credit = std::max(0.0, credit - 1.0);
            if (pulseIndex == pendingFireTicks_.size())
                pendingFireTicks_.push_back({nextFireTick_++, activeRaidSpaceId()});
            auto &pulse = pendingFireTicks_[pulseIndex++];
            if (id == kInvalidCombatTargetId) pulse.playerDamage = window.damage;
            else pulse.enemies.emplace_back(id, window.damage);
        }
        previous = window.until;
    }
    if (previous + timeTolerance < until) credit = 0;
}

std::optional<RaidFireDamageTick> GameplayWorld::prepareFireDamageTick() const
{
    if (!hasPendingFireDamage() || !raidSession_.isActive()) return std::nullopt;
    const auto &pulse = pendingFireTicks_.front();
    if (pulse.space != activeRaidSpaceId()) return std::nullopt;
    RaidFireDamageTick plan;
    plan.sequence = pulse.sequence;
    plan.space = pulse.space;
    plan.playerPosition = player_.position();
    plan.playerHealth = player_.health();
    if (pulse.playerDamage) plan.playerDamage = CombatDamageCommand{pulse.playerDamage, HitRegion::Torso, 0, 0, false, {}};
    for (const auto &[id, damage] : pulse.enemies)
    {
        const auto *enemy = activeEnemies().find(id);
        if (!enemy || enemy->isDead()) continue;
        const auto armor = enemy->torsoArmor();
        const auto resolved = resolveCombatDamage({damage, HitRegion::Torso, 0, 0, false,
            armor ? std::optional{armor->protection()} : std::nullopt});
        if (!resolved.resolved()) return std::nullopt;
        plan.enemies.push_back({id, enemy->position(), enemy->health(), armor, resolved});
    }
    return plan;
}

bool GameplayWorld::validatePreparedFireDamageTick(const RaidFireDamageTick &plan) const
{
    if (!raidSession_.isActive() || !hasPendingFireDamage()) return false;
    const auto &pulse = pendingFireTicks_.front();
    if (plan.sequence != pulse.sequence || plan.space != activeRaidSpaceId() || plan.space != pulse.space ||
        plan.playerHealth != player_.health() || plan.playerPosition.x != player_.position().x ||
        plan.playerPosition.y != player_.position().y || plan.playerDamage.has_value() != (pulse.playerDamage > 0))
        return false;
    if (plan.playerDamage && (plan.playerDamage->baseDamage != pulse.playerDamage ||
        plan.playerDamage->region != HitRegion::Torso || plan.playerDamage->penetration != 0 ||
        plan.playerDamage->armorDamage != 0 || plan.playerDamage->weakPoint || plan.playerDamage->armor ||
        plan.playerDamage->unarmoredDamageBonus != 0)) return false;
    return validateAreaDamageTargets(plan.enemies);
}

bool GameplayWorld::commitPreparedFireDamageTick(const RaidFireDamageTick &plan)
{
    if (!validatePreparedFireDamageTick(plan)) return false;
    if (!plan.enemies.empty()) applyAreaDamageTargets(plan.enemies);
    pendingFireTicks_.pop_front();
    return true;
}
