#pragma once
#include "combat_runtime_checkpoint.h"
#include <nlohmann/json.hpp>

// Optional values are explicit nullable JSON, without a global optional ADL
// specialization that could alter the repository's other save contracts.
inline void to_json(nlohmann::json &j, const EnemyRuntimeCheckpoint &v)
{
#define FIELD(name) j[#name] = v.name
    FIELD(id);
    FIELD(position);
    FIELD(size);
    FIELD(velocity);
    FIELD(health);
    FIELD(maximumHealth);
    FIELD(facing);
    FIELD(movement);
    FIELD(role);
    FIELD(awareness);
    FIELD(attackPhase);
    FIELD(attackDirection);
    FIELD(aiMoveDirection);
    FIELD(attackRemaining);
    FIELD(impactSlowRemaining);
    FIELD(grabCooldown);
    FIELD(scratchCooldown);
    FIELD(specialChargeHold);
    FIELD(searchRemaining);
    FIELD(specialChargeArmed);
    FIELD(hitConsumed);
    FIELD(activeOpportunityPending);
    FIELD(navigationRefreshRemaining);
#undef FIELD
    j["attackType"] = v.attackType ? nlohmann::json(*v.attackType) : nlohmann::json(nullptr);
    j["lastKnownTarget"] =
        v.lastKnownTarget ? nlohmann::json(*v.lastKnownTarget) : nlohmann::json(nullptr);
    j["navigationTarget"] =
        v.navigationTarget ? nlohmann::json(*v.navigationTarget) : nlohmann::json(nullptr);
}
inline void from_json(const nlohmann::json &j, EnemyRuntimeCheckpoint &v)
{
#define FIELD(name) j.at(#name).get_to(v.name)
    FIELD(id);
    FIELD(position);
    FIELD(size);
    FIELD(velocity);
    FIELD(health);
    FIELD(maximumHealth);
    FIELD(facing);
    FIELD(movement);
    FIELD(role);
    FIELD(awareness);
    FIELD(attackPhase);
    FIELD(attackDirection);
    FIELD(aiMoveDirection);
    FIELD(attackRemaining);
    FIELD(impactSlowRemaining);
    FIELD(grabCooldown);
    FIELD(scratchCooldown);
    FIELD(specialChargeHold);
    FIELD(searchRemaining);
    FIELD(specialChargeArmed);
    FIELD(hitConsumed);
    FIELD(activeOpportunityPending);
    FIELD(navigationRefreshRemaining);
#undef FIELD
    v.attackType = j.at("attackType").is_null()
                       ? std::nullopt
                       : std::optional{j.at("attackType").get<std::uint32_t>()};
    v.lastKnownTarget = j.at("lastKnownTarget").is_null()
                            ? std::nullopt
                            : std::optional{j.at("lastKnownTarget").get<CheckpointPoint>()};
    v.navigationTarget = j.at("navigationTarget").is_null()
                             ? std::nullopt
                             : std::optional{j.at("navigationTarget").get<CheckpointPoint>()};
}
/* Field order is intentionally documented by the named schema above:
    id, position, size, velocity, health, maximumHealth, facing, movement, role,
    awareness, attackPhase, attackType, attackDirection, aiMoveDirection,
    lastKnownTarget, attackRemaining, impactSlowRemaining, grabCooldown,
    scratchCooldown, specialChargeHold, searchRemaining, specialChargeArmed,
    hitConsumed, activeOpportunityPending, navigationTarget,
    navigationRefreshRemaining */
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(LogicalFlightCheckpoint, id, origin, position, direction, impact,
                                   speed, extent, travelled, maximumDistance, damage, penetration,
                                   aimedTarget, aimedRegion, tracerStyle, weakPoint, tracerLength,
                                   tracerOpacity, tracerLifetime)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(WorldShootingCheckpoint, nextShotId, flights, fireConfig,
                                   fireState, spreadSeed, spreadRandomState, spreadRandomIncrement,
                                   burstShotCount, aimConfig, aimVectors, aimDownSightsProgress,
                                   recoilBendRemaining, aimControlMode, aimInitialized, recoilSeed,
                                   recoilRandomState, recoilRandomIncrement, weaponDamage,
                                   weaponPenetration, maximumRange, logicalSpeed, tracerStyle,
                                   tracerLength, tracerOpacity, tracerLifetime, initialized)
