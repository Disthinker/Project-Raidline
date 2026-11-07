#pragma once

#include "throwable_types.h"
#include "combat_damage_domain.h"
#include "enemy_armor.h"

struct GrenadeEnemyDamage
{
    CombatTargetId id{};
    Vec2 position{};
    int health{};
    std::optional<EnemyArmorState> armor;
    CombatDamageResolution damage;
};
struct GrenadeExplosionPlan
{
    GrenadeExplosion explosion;
    std::vector<GrenadeEnemyDamage> enemies;
    std::optional<CombatDamageCommand> playerDamage;
    Vec2 playerPosition{};
    int playerHealth{};
};
