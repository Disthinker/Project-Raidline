#pragma once

#include "collision.h"
#include "enemy.h"
#include "player_damage_observation.h"
#include <cmath>
#include <optional>

inline constexpr float kEnemyContactProtectionSeconds = 0.25F;

enum class EnemyContactDisposition { NoContact, Suppressed, Applied };

struct EnemyAttackContactResult
{
    EnemyContactDisposition disposition{EnemyContactDisposition::NoContact};
    std::optional<PlayerDamageObservation> damage;
    int legacyDamage{};
    float controlDuration{};
};

// Mechanism only. Activity chooses the eligible target and supplies its LOS.
// LOS is lazy: never query navigation/occlusion for a dead, idle or distant actor.
// The existing timer is owned/advanced/checkpointed by the activity, not here.
template <class LineOfSight>
[[nodiscard]] EnemyAttackContactResult resolveEnemyAttackContact(
    Enemy &enemy, Rect targetBounds, bool targetEligible,
    float &protectionRemaining, LineOfSight lineOfSight)
{
    if (!targetEligible || enemy.isDead() ||
        !std::isfinite(protectionRemaining) || protectionRemaining < 0.0F ||
        (!enemy.hasGrabContactOpportunity() && !enemy.hasAttackHitOpportunity()))
        return {};
    const auto hitbox = enemy.attackHitbox();
    if (!hitbox || !isCollision(*hitbox, targetBounds) || !lineOfSight())
        return {};

    // Grab deals no damage itself. Contact commits the existing owned Bite
    // follow-up immediately, even when its damage will be suppressed.
    if (enemy.hasGrabContactOpportunity() && !enemy.confirmGrabContact())
        return {};
    const auto type = enemy.attackType();
    const auto config = enemy.attackConfig();
    if (!type || !config || !enemy.hasAttackHitOpportunity() || !enemy.consumeAttackHit())
        return {};
    if (protectionRemaining > 0.0F)
        return {EnemyContactDisposition::Suppressed, std::nullopt, 0, 0.0F};

    protectionRemaining = kEnemyContactProtectionSeconds;
    return {EnemyContactDisposition::Applied,
            enemyAttackDamageObservation(enemy.combatTargetId(), *type),
            config->damage, config->controlDuration};
}
