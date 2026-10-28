#pragma once
#include "combat_runtime_checkpoint.h"
#include "enemy_variant.h"
#include <cmath>
#include <nlohmann/json.hpp>
#include <stdexcept>

// The identity and values are one frozen versioned contract. Loading never
// looks up a current content definition or derives the variant from health.
inline nlohmann::json enemyVariantSnapshotJson(const EnemyVariantSnapshot &v)
{
    if (!validEnemyVariantSnapshot(v))
        throw std::invalid_argument("Invalid enemy variant snapshot");
    return {{"definition_id", enemyVariantDefinitionId(v.kind)},
            {"normal_move_speed", v.movement.normalMoveSpeed},
            {"visible_pursuit_move_speed", v.movement.visiblePursuitMoveSpeed
                ? nlohmann::json(*v.movement.visiblePursuitMoveSpeed)
                : nlohmann::json(nullptr)},
            {"attack_profile", kStandardInfectedAttackProfileId}};
}

inline EnemyVariantSnapshot parseEnemyVariantSnapshotJson(const nlohmann::json &j)
{
    if (!j.is_object() || j.size() != 4 || !j.at("definition_id").is_string() ||
        !j.at("normal_move_speed").is_number() ||
        (!j.at("visible_pursuit_move_speed").is_null() &&
         !j.at("visible_pursuit_move_speed").is_number()) ||
        !j.at("attack_profile").is_string() ||
        j.at("attack_profile").get<std::string>() != kStandardInfectedAttackProfileId)
        throw std::invalid_argument("Invalid enemy variant fields or attack profile");
    const auto kind = enemyVariantKindFromDefinitionId(j.at("definition_id").get<std::string>());
    if (!kind) throw std::invalid_argument("Unknown enemy variant identity");
    EnemyVariantSnapshot result{*kind, {j.at("normal_move_speed").get<float>(),
        std::nullopt, kStandardInfectedAttackProfileVersion}};
    if (!j.at("visible_pursuit_move_speed").is_null())
        result.movement.visiblePursuitMoveSpeed = j.at("visible_pursuit_move_speed").get<float>();
    if (!std::isfinite(result.movement.normalMoveSpeed) ||
        (result.movement.visiblePursuitMoveSpeed &&
         !std::isfinite(*result.movement.visiblePursuitMoveSpeed)) ||
        !validEnemyVariantSnapshot(result))
        throw std::invalid_argument("Invalid frozen enemy movement values");
    return result;
}

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
    if (v.torsoArmor) j["torsoArmor"] = {v.torsoArmor->protectionRequirement, v.torsoArmor->durability, v.torsoArmor->durabilityLossBasisPoints};
    j["attackType"] = v.attackType ? nlohmann::json(*v.attackType) : nlohmann::json(nullptr);
    j["lastKnownTarget"] =
        v.lastKnownTarget ? nlohmann::json(*v.lastKnownTarget) : nlohmann::json(nullptr);
    j["navigationTarget"] =
        v.navigationTarget ? nlohmann::json(*v.navigationTarget) : nlohmann::json(nullptr);
    j["variant"] = v.variant ? enemyVariantSnapshotJson(*v.variant) : nlohmann::json(nullptr);
    j["visible_pursuit_move"] = v.visiblePursuitMove;
}
inline void from_json(const nlohmann::json &j, EnemyRuntimeCheckpoint &v)
{
    // A checkpoint predating schema 58 intentionally has no variant. Do not
    // manufacture a new ordinary profile and replace its old AI configuration.
    v.variant = j.contains("variant") && !j.at("variant").is_null()
        ? std::optional{parseEnemyVariantSnapshotJson(j.at("variant"))} : std::nullopt;
    if (j.contains("visible_pursuit_move") && !j.at("visible_pursuit_move").is_boolean())
        throw std::invalid_argument("Invalid visible pursuit checkpoint flag");
    v.visiblePursuitMove = j.value("visible_pursuit_move", false);
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
    v.torsoArmor.reset();
    if (j.contains("torsoArmor")) { const auto &a = j.at("torsoArmor"); v.torsoArmor = EnemyArmorState{a.at(0).get<int>(), a.at(1).get<std::uint32_t>(), a.at(2).get<std::uint32_t>()}; }
    v.attackType = j.at("attackType").is_null()
                       ? std::nullopt
                       : std::optional{j.at("attackType").get<std::uint32_t>()};
    v.lastKnownTarget = j.at("lastKnownTarget").is_null()
                            ? std::nullopt
                            : std::optional{j.at("lastKnownTarget").get<CheckpointPoint>()};
    v.navigationTarget = j.at("navigationTarget").is_null()
                             ? std::nullopt
                             : std::optional{j.at("navigationTarget").get<CheckpointPoint>()};
    if ((v.variant || v.visiblePursuitMove) && !validateEnemyRuntimeCheckpoint(v))
        throw std::invalid_argument("Invalid enemy variant checkpoint state");
}
/* Field order is intentionally documented by the named schema above:
    id, position, size, velocity, health, maximumHealth, facing, movement, role,
    awareness, attackPhase, attackType, attackDirection, aiMoveDirection,
    lastKnownTarget, attackRemaining, impactSlowRemaining, grabCooldown,
    scratchCooldown, specialChargeHold, searchRemaining, specialChargeArmed,
    hitConsumed, activeOpportunityPending, navigationTarget,
    navigationRefreshRemaining, torsoArmor, variant, visible_pursuit_move */
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
