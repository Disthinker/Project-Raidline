#include "base_defense_state.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <set>

namespace
{
void number(std::uint64_t &h, std::uint64_t v) noexcept
{
    for (int i = 0; i < 8; ++i)
    {
        h ^= (v & 255U);
        h *= 1099511628211ULL;
        v >>= 8U;
    }
}
void real(std::uint64_t &h, float v) noexcept { number(h, std::bit_cast<std::uint32_t>(v)); }
void point(std::uint64_t &h, Vec2 p) noexcept
{
    real(h, p.x);
    real(h, p.y);
}
void rect(std::uint64_t &h, Rect r) noexcept
{
    point(h, r.position);
    point(h, r.size);
}
void text(std::uint64_t &h, const std::string &s) noexcept
{
    number(h, s.size());
    for (const unsigned char c : s)
    {
        h ^= c;
        h *= 1099511628211ULL;
    }
}
bool nonnegative(float f) { return std::isfinite(f) && f >= 0.0F; }
bool inWorld(Vec2 p, Vec2 world)
{
    return nonnegative(p.x) && nonnegative(p.y) && p.x <= world.x && p.y <= world.y;
}
bool inside(Vec2 p, Rect r)
{
    return p.x >= r.position.x && p.y >= r.position.y && p.x <= r.position.x + r.size.x &&
           p.y <= r.position.y + r.size.y;
}
bool validRect(Rect r, Vec2 world)
{
    return inWorld(r.position, world) && std::isfinite(r.size.x) && std::isfinite(r.size.y) &&
           r.size.x > 0.0F && r.size.y > 0.0F &&
           inWorld({r.position.x + r.size.x, r.position.y + r.size.y}, world);
}
bool overlaps(Rect a, Rect b)
{
    return a.position.x < b.position.x + b.size.x && a.position.x + a.size.x > b.position.x &&
           a.position.y < b.position.y + b.size.y && a.position.y + a.size.y > b.position.y;
}
} // namespace

std::uint64_t baseDefenseLayoutHash(const BaseDefenseSnapshot &s) noexcept
{
    std::uint64_t h = 14695981039346656037ULL;
    text(h, s.eventId);
    number(h, s.siegeSequence);
    number(h, s.rulesVersion);
    text(h, s.siteDefinitionId);
    text(h, s.plotId);
    text(h, s.layoutIdentity);
    number(h, s.seed);
    point(h, s.worldSize);
    rect(h, s.safeCore);
    number(h, s.movementBlockers.size());
    for (const auto &r : s.movementBlockers)
        rect(h, r);
    number(h, s.corridors.size());
    for (const auto &r : s.corridors)
        rect(h, r);
    number(h, s.coreDefenseZones.size());
    for (const auto &r : s.coreDefenseZones)
        rect(h, r);
    number(h, s.wavePlans.size());
    for (const auto &w : s.wavePlans)
    {
        real(h, w.releaseSeconds);
        point(h, w.entry);
        point(h, w.target);
        number(h, w.route.size());
        for (const auto p : w.route)
            point(h, p);
        number(h, w.enemyIds.size());
        for (const auto id : w.enemyIds)
            number(h, id);
        number(h, static_cast<std::uint64_t>(w.enemyMaxHealth));
    }
    number(h, s.frozenPopulation);
    number(h, s.frozenMoraleTier);
    number(h, s.frozenSiteThreat);
    number(h, s.breachLimit);
    number(h, s.maximumActiveEnemies);
    return h;
}

bool validateBaseDefenseSnapshot(const BaseDefenseSnapshot &s, std::string &message)
{
    auto fail = [&](const char *why)
    {
        message = why;
        return false;
    };
    if (s.eventId.empty() || s.siegeSequence == 0U || s.mode != BaseDefenseMode::Realtime ||
        s.rulesVersion != kBaseDefenseRulesVersion || s.siteDefinitionId.empty() ||
        s.layoutIdentity.empty() || !std::isfinite(s.worldSize.x) ||
        !std::isfinite(s.worldSize.y) || s.worldSize.x <= 0.0F || s.worldSize.y <= 0.0F ||
        !validRect(s.safeCore, s.worldSize) || s.wavePlans.size() != 3U ||
        s.movementBlockers.size() > 8192U || s.corridors.empty() || s.corridors.size() > 256U ||
        s.coreDefenseZones.empty() || s.coreDefenseZones.size() > 2U ||
        s.breachLimit != kBaseDefenseBreachLimit ||
        s.maximumActiveEnemies != kBaseDefenseMaximumActiveEnemies || s.frozenMoraleTier > 2U ||
        s.layoutHash != baseDefenseLayoutHash(s))
        return fail("Defense identity, layout or rule contract is invalid");
    for (const auto &r : s.movementBlockers)
        if (!validRect(r, s.worldSize))
            return fail("Defense frozen obstacle is invalid");
    for (const auto &r : s.corridors)
        if (!validRect(r, s.worldSize) || overlaps(r, s.safeCore))
            return fail("Defense corridor is invalid");
    for (const auto &r : s.coreDefenseZones)
        if (!validRect(r, s.worldSize) || overlaps(r, s.safeCore))
            return fail("Defense line is invalid");
    std::set<std::uint64_t> planned;
    std::vector<std::uint64_t> spawnOrder;
    float previousRelease = -1.0F;
    for (const auto &w : s.wavePlans)
    {
        if (!nonnegative(w.releaseSeconds) || w.releaseSeconds < previousRelease ||
            !inWorld(w.entry, s.worldSize) || !inWorld(w.target, s.worldSize) ||
            inside(w.entry, s.safeCore) || w.route.size() < 2U || w.route.size() > 4096U ||
            w.enemyIds.empty() || w.enemyMaxHealth < 1 || w.enemyMaxHealth > 10000)
            return fail("Defense wave or route is invalid");
        if (!std::any_of(s.coreDefenseZones.begin(), s.coreDefenseZones.end(),
                         [&](Rect r) { return inside(w.target, r); }) ||
            std::hypot(w.route.front().x - w.entry.x, w.route.front().y - w.entry.y) > 0.1F ||
            std::hypot(w.route.back().x - w.target.x, w.route.back().y - w.target.y) > 0.1F)
            return fail("Defense route endpoints do not match the frozen line");
        previousRelease = w.releaseSeconds;
        for (const auto p : w.route)
            if (!inWorld(p, s.worldSize))
                return fail("Defense route is outside the world");
        for (const auto id : w.enemyIds)
        {
            if (id == 0U || !planned.insert(id).second)
                return fail("Defense enemy ID is duplicated");
            spawnOrder.push_back(id);
        }
    }
    if (planned.size() < 24U || planned.size() > 36U || s.spawnedEnemyCount > planned.size() ||
        s.currentWave > s.wavePlans.size() || s.enemies.size() > s.maximumActiveEnemies ||
        !nonnegative(s.elapsedSeconds) || !nonnegative(s.nextSpawnDelay) ||
        !inWorld(s.playerPosition, s.worldSize) || !nonnegative(s.damageProtectionSeconds) ||
        s.damageProtectionSeconds > 0.251F || s.activeWeaponSlot > 2U ||
        !std::isfinite(s.pendingWorldSeconds) || s.pendingWorldSeconds < 0.0 ||
        !nonnegative(s.baseCombatElapsedSeconds) || !nonnegative(s.medicalTickAccumulatorSeconds))
        return fail("Defense progress or player checkpoint is invalid");
    std::set<std::uint64_t> resolved;
    for (const auto &ids : {s.killedIds, s.breachedIds})
        for (const auto id : ids)
            if (!planned.contains(id) || !resolved.insert(id).second)
                return fail("Defense resolution ID is unknown or duplicated");
    if (resolved.size() + s.enemies.size() != s.spawnedEnemyCount)
        return fail("Defense spawned population is not conserved");
    // Runtime checkpoint structure validators also verify private attack,
    // cooldown, ballistic and PRNG state rather than silently resetting it.
    std::set<std::uint64_t> active;
    for (const auto &enemy : s.enemies)
    {
        if (!validateEnemyRuntimeCheckpoint(enemy) ||
            !inWorld({enemy.position[0], enemy.position[1]}, s.worldSize) ||
            !planned.contains(enemy.id) || resolved.contains(enemy.id) ||
            !active.insert(enemy.id).second)
            return fail("Defense enemy checkpoint is invalid");
        if (enemy.size != CheckpointPoint{32.0F, 48.0F} ||
            !inWorld({enemy.position[0] + enemy.size[0], enemy.position[1] + enemy.size[1]},
                     s.worldSize) ||
            inside({enemy.position[0] + enemy.size[0] * 0.5F,
                    enemy.position[1] + enemy.size[1] * 0.5F},
                   s.safeCore))
            return fail("Defense enemy footprint is invalid");
        for (const auto &wave : s.wavePlans)
            if (std::find(wave.enemyIds.begin(), wave.enemyIds.end(), enemy.id) !=
                    wave.enemyIds.end() &&
                enemy.maximumHealth != wave.enemyMaxHealth)
                return fail("Defense enemy health differs from its frozen wave");
    }
    for (std::size_t i = 0; i < spawnOrder.size(); ++i)
    {
        const bool exists = active.contains(spawnOrder[i]) || resolved.contains(spawnOrder[i]);
        if (exists != (i < s.spawnedEnemyCount))
            return fail("Defense enemy progress does not match the frozen spawn order");
    }
    std::set<std::uint64_t> contacts;
    std::set<std::uint64_t> reserved;
    for (const auto id : s.reservedAttackers)
        if (!active.contains(id) || !reserved.insert(id).second)
            return fail("Defense attack reservation is invalid");
    for (const auto &c : s.contacts)
        if (!active.contains(c.enemyId) || !contacts.insert(c.enemyId).second ||
            !nonnegative(c.seconds))
            return fail("Defense line contact is invalid");
    if (!validateWorldShootingCheckpoint(s.shooting))
        return fail("Defense shooting checkpoint is invalid");
    if ((s.elapsedSeconds > 0.0F || s.spawnedEnemyCount > 0U) && !s.shooting.initialized)
        return fail("Active defense shooting state cannot be reset");
    message.clear();
    return true;
}

std::uint64_t baseDefenseCheckpointHash(const BaseDefenseSnapshot &s) noexcept
{
    std::uint64_t h = baseDefenseLayoutHash(s);
    number(h, s.layoutHash);
    real(h, s.elapsedSeconds);
    number(h, s.currentWave);
    number(h, s.spawnedEnemyCount);
    real(h, s.nextSpawnDelay);
    number(h, s.navigationScheduleCursor);
    number(h, s.attackScheduleCursor);
    number(h, s.reservedAttackers.size());
    for (auto n : s.reservedAttackers)
        number(h, n);
    number(h, s.killedIds.size());
    for (auto n : s.killedIds)
        number(h, n);
    number(h, s.breachedIds.size());
    for (auto n : s.breachedIds)
        number(h, n);
    number(h, s.contacts.size());
    for (auto c : s.contacts)
    {
        number(h, c.enemyId);
        real(h, c.seconds);
    }
    auto checkpointPointHash = [&](CheckpointPoint p)
    {
        real(h, p[0]);
        real(h, p[1]);
    };
    number(h, s.enemies.size());
    for (const auto &e : s.enemies)
    {
        number(h, e.id);
        checkpointPointHash(e.position);
        checkpointPointHash(e.size);
        checkpointPointHash(e.velocity);
        number(h, e.health);
        number(h, e.maximumHealth);
        number(h, e.facing);
        number(h, e.movement);
        number(h, e.role);
        number(h, e.awareness);
        number(h, e.attackPhase);
        number(h, e.attackType.has_value());
        if (e.attackType)
            number(h, *e.attackType);
        checkpointPointHash(e.attackDirection);
        checkpointPointHash(e.aiMoveDirection);
        number(h, e.lastKnownTarget.has_value());
        if (e.lastKnownTarget)
            checkpointPointHash(*e.lastKnownTarget);
        real(h, e.attackRemaining);
        real(h, e.impactSlowRemaining);
        real(h, e.grabCooldown);
        real(h, e.scratchCooldown);
        real(h, e.specialChargeHold);
        real(h, e.searchRemaining);
        number(h, e.specialChargeArmed);
        number(h, e.hitConsumed);
        number(h, e.activeOpportunityPending);
        number(h, e.navigationTarget.has_value());
        if (e.navigationTarget)
            checkpointPointHash(*e.navigationTarget);
        real(h, e.navigationRefreshRemaining);
    }
    point(h, s.playerPosition);
    real(h, s.damageProtectionSeconds);
    const auto &w = s.shooting;
    number(h, w.nextShotId);
    number(h, w.flights.size());
    for (const auto &f : w.flights)
    {
        number(h, f.id);
        checkpointPointHash(f.origin);
        checkpointPointHash(f.position);
        checkpointPointHash(f.direction);
        checkpointPointHash(f.impact);
        real(h, f.speed);
        real(h, f.extent);
        real(h, f.travelled);
        real(h, f.maximumDistance);
        number(h, f.damage);
        number(h, f.penetration);
        number(h, f.aimedTarget);
        number(h, f.aimedRegion);
        number(h, f.tracerStyle);
        number(h, f.weakPoint);
        real(h, f.tracerLength);
        real(h, f.tracerOpacity);
        real(h, f.tracerLifetime);
    }
    for (auto f : w.fireConfig)
        real(h, f);
    for (auto f : w.fireState)
        real(h, f);
    number(h, w.spreadSeed);
    number(h, w.spreadRandomState);
    number(h, w.spreadRandomIncrement);
    number(h, w.burstShotCount);
    for (auto f : w.aimConfig)
        real(h, f);
    for (auto p : w.aimVectors)
        checkpointPointHash(p);
    real(h, w.aimDownSightsProgress);
    real(h, w.recoilBendRemaining);
    number(h, w.aimControlMode);
    number(h, w.aimInitialized);
    number(h, w.recoilSeed);
    number(h, w.recoilRandomState);
    number(h, w.recoilRandomIncrement);
    number(h, w.weaponDamage);
    number(h, w.weaponPenetration);
    real(h, w.maximumRange);
    real(h, w.logicalSpeed);
    number(h, w.tracerStyle);
    real(h, w.tracerLength);
    real(h, w.tracerOpacity);
    real(h, w.tracerLifetime);
    number(h, w.initialized);
    number(h, s.activeWeaponSlot);
    number(h, s.commandSequence);
    number(h, s.weaponFaultSequence);
    number(h, s.medicalRandomSequence);
    number(h, s.woundRandomSequence);
    number(h, std::bit_cast<std::uint64_t>(s.pendingWorldSeconds));
    real(h, s.baseCombatElapsedSeconds);
    real(h, s.medicalTickAccumulatorSeconds);
    return h;
}
