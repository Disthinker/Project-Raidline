#include "combat_runtime_checkpoint.h"
#include <algorithm>
#include <cmath>

namespace
{
bool point(CheckpointPoint p) { return std::isfinite(p[0]) && std::isfinite(p[1]); }
bool nonnegative(float f) { return std::isfinite(f) && f >= 0; }
} // namespace
bool validateEnemyRuntimeCheckpoint(const EnemyRuntimeCheckpoint &e) noexcept
{
    return e.id != 0 && point(e.position) && point(e.size) && point(e.velocity) && e.size[0] > 0 &&
           e.size[1] > 0 && e.health > 0 && e.health <= e.maximumHealth && e.facing <= 1 &&
           e.movement <= 2 && e.role <= 2 && e.awareness <= 2 && e.attackPhase <= 4 &&
           (!e.attackType || *e.attackType <= 2) &&
           (e.attackPhase == 0) == !e.attackType.has_value() && point(e.attackDirection) &&
           point(e.aiMoveDirection) && (!e.lastKnownTarget || point(*e.lastKnownTarget)) &&
           (!e.navigationTarget || point(*e.navigationTarget)) && nonnegative(e.attackRemaining) &&
           nonnegative(e.impactSlowRemaining) && nonnegative(e.grabCooldown) &&
           nonnegative(e.scratchCooldown) && nonnegative(e.specialChargeHold) &&
           nonnegative(e.searchRemaining) && nonnegative(e.navigationRefreshRemaining);
}
bool validateWorldShootingCheckpoint(const WorldShootingCheckpoint &s) noexcept
{
    if (!s.initialized)
        return s.flights.empty() && s.nextShotId != 0;
    if (s.nextShotId == 0 || s.flights.size() > 128 || s.aimControlMode > 1 ||
        !(s.spreadRandomIncrement & 1U) || !(s.recoilRandomIncrement & 1U) || s.weaponDamage <= 0 ||
        s.weaponPenetration < 0 || !nonnegative(s.maximumRange) || s.maximumRange <= 0 ||
        !nonnegative(s.logicalSpeed) || s.logicalSpeed <= 0 || s.tracerStyle > 1 ||
        !nonnegative(s.tracerLength) || !nonnegative(s.tracerOpacity) || s.tracerOpacity > 1 ||
        !nonnegative(s.tracerLifetime) || s.tracerLifetime <= 0 ||
        !nonnegative(s.aimDownSightsProgress) || s.aimDownSightsProgress > 1 ||
        !nonnegative(s.recoilBendRemaining))
        return false;
    for (float f : s.fireConfig)
        if (!nonnegative(f))
            return false;
    for (float f : s.fireState)
        if (!nonnegative(f))
            return false;
    for (float f : s.aimConfig)
        if (!nonnegative(f))
            return false;
    for (auto p : s.aimVectors)
        if (!point(p))
            return false;
    if (s.fireConfig[0] <= 0 || s.fireConfig[1] > s.fireConfig[2] || s.fireConfig[6] <= 0 ||
        s.fireConfig[6] > 1 || s.fireConfig[7] <= 0 || s.fireConfig[7] > 1 || s.fireConfig[8] > 1 ||
        s.fireConfig[9] > 1 || s.fireConfig[12] <= s.fireConfig[11] || s.fireConfig[13] > 1 ||
        s.fireConfig[14] > 1 || s.fireConfig[15] < 1 || s.aimConfig[0] <= 0 ||
        s.aimConfig[1] <= 0 || s.aimConfig[3] <= 0 || s.aimConfig[4] > 1 || s.aimConfig[5] <= 0 ||
        s.aimConfig[6] <= 0 || s.aimConfig[7] <= 0 || s.aimConfig[7] > s.aimConfig[8])
        return false;
    for (std::size_t i = 0; i < s.flights.size(); ++i)
    {
        const auto &f = s.flights[i];
        if (!f.id || f.id >= s.nextShotId || !point(f.origin) || !point(f.position) ||
            !point(f.direction) || !point(f.impact) || !nonnegative(f.speed) || f.speed <= 0 ||
            !nonnegative(f.extent) || !nonnegative(f.travelled) ||
            !nonnegative(f.maximumDistance) || f.maximumDistance <= f.travelled || f.damage <= 0 ||
            f.penetration < 0 || f.aimedRegion > 2 || f.tracerStyle > 1 ||
            !nonnegative(f.tracerLength) || !nonnegative(f.tracerOpacity) || f.tracerOpacity > 1 ||
            !nonnegative(f.tracerLifetime) || f.tracerLifetime <= 0)
            return false;
        if (std::abs(std::hypot(f.direction[0], f.direction[1]) - 1) > 0.001F)
            return false;
        for (std::size_t j = 0; j < i; ++j)
            if (s.flights[j].id == f.id)
                return false;
        for (std::size_t axis = 0; axis < 2; ++axis)
        {
            if (std::abs(f.position[axis] - f.origin[axis] - f.direction[axis] * f.travelled) >
                    0.1F ||
                std::abs(f.impact[axis] - f.origin[axis] - f.direction[axis] * f.maximumDistance) >
                    0.1F)
                return false;
        }
    }
    return true;
}
