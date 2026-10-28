#pragma once
#include "enemy_armor.h"
#include <optional>

#include "definition_id.h"
#include "enemy_variant.h"

// Definition identity is not an actor identity. Resolved values are frozen into
// existing activity snapshots; loading a snapshot never consults new balance.
struct EnemyCombatDefinition
{
    EnemyCombatDefinitionId id;
    int maximumHealth{};
    std::optional<EnemyArmorState> torsoArmor;
    std::optional<EnemyMovementProfile> movement;
};

inline EnemyCombatDefinitionId ordinaryInfectedDefinitionId()
{
    return EnemyCombatDefinitionId{"enemy.infected.basic"};
}
