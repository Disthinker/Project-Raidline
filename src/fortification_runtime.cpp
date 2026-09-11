#include "fortification_runtime.h"
#include "collision.h"
#include "raid_space_query.h"
#include <algorithm>
#include <cmath>

bool FortificationRuntime::restore(std::span<const FortificationSnapshot> snapshots,
                                   std::uint32_t revision)
{
    if (!validateFortificationCheckpoints(snapshots, revision))
        return false;
    states_.assign(snapshots.begin(), snapshots.end());
    geometryRevision_ = revision;
    beginFrame();
    return true;
}

void FortificationRuntime::beginFrame() noexcept
{
    damage_.clear();
    disabled_.clear();
}
const FortificationSnapshot *FortificationRuntime::find(FortificationInstanceId id) const noexcept
{
    const auto it =
        std::find_if(states_.begin(), states_.end(), [&](const auto &s) { return s.id == id; });
    return it == states_.end() ? nullptr : &*it;
}
bool FortificationRuntime::clear(Rect body) const noexcept
{
    return std::none_of(states_.begin(), states_.end(), [&](const auto &s) {
        return s.durability && isCollision(body, s.footprint);
    });
}
bool FortificationRuntime::lineOfSight(Vec2 from, Vec2 to,
                                       FortificationInstanceId ignored) const noexcept
{
    return std::none_of(states_.begin(), states_.end(), [&](const auto &s) {
        return s.id != ignored && s.durability &&
               raidSpaceSegmentIntersectsBlockerInterior(from, to, s.footprint);
    });
}
Vec2 FortificationRuntime::surfacePoint(Rect r, Vec2 from) noexcept
{
    return {std::clamp(from.x, r.position.x, r.position.x + r.size.x),
            std::clamp(from.y, r.position.y, r.position.y + r.size.y)};
}
const FortificationSnapshot *FortificationRuntime::obstructing(Vec2 from, Vec2 goal) const noexcept
{
    const FortificationSnapshot *best = nullptr;
    float closest = 260.0F;
    for (const auto &s : states_)
    {
        if (!s.durability || !raidSpaceSegmentIntersectsBlockerInterior(from, goal, s.footprint))
            continue;
        const auto surface = surfacePoint(s.footprint, from);
        const auto distance = std::hypot(surface.x - from.x, surface.y - from.y);
        if (distance < closest || (distance == closest && best && s.id < best->id))
        {
            closest = distance;
            best = &s;
        }
    }
    return best;
}
Vec2 FortificationRuntime::resolveMovement(Rect before, Vec2 desired) const noexcept
{
    for (const auto &s : states_)
        if (s.durability)
            desired.x = resolveHorizontalCollision(before, desired.x, s.footprint);
    for (const auto &s : states_)
        if (s.durability)
            desired.y = resolveVerticalCollision({{desired.x, before.position.y}, before.size},
                                                 desired.y, s.footprint);
    return desired;
}
bool FortificationRuntime::consumeScratch(Enemy &enemy, FortificationInstanceId id,
                                          const RaidSpaceBlockerIndex &geometry)
{
    const auto it =
        std::find_if(states_.begin(), states_.end(), [&](const auto &s) { return s.id == id; });
    if (it == states_.end() || !it->durability || enemy.isDead() || !enemy.combatTargetId() ||
        enemy.attackType() != EnemyAttackType::Scratch || !enemy.hasAttackHitOpportunity())
        return false;
    const auto hit = enemy.attackHitbox();
    const Vec2 center{enemy.position().x + enemy.size().x / 2,
                      enemy.position().y + enemy.size().y / 2};
    const auto surface = surfacePoint(it->footprint, center);
    if (!hit || !isCollision(*hit, it->footprint) || !geometry.hasLineOfSight(center, surface) ||
        !lineOfSight(center, surface, id) || !enemy.consumeAttackHit())
        return false;
    const auto amount = std::min(12U, it->durability);
    it->durability -= amount;
    damage_.push_back({enemy.combatTargetId(), id, amount, it->durability});
    if (!it->durability)
    {
        ++geometryRevision_;
        disabled_.push_back({enemy.combatTargetId(), id, it->footprint});
    }
    return true;
}
