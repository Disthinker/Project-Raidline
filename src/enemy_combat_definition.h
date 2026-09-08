#pragma once

#include "definition_id.h"

// Definition identity is not an actor identity. Resolved values are frozen into
// existing activity snapshots; loading a snapshot never consults new balance.
struct EnemyCombatDefinition
{
    EnemyCombatDefinitionId id;
    int maximumHealth{};
};

inline EnemyCombatDefinitionId ordinaryInfectedDefinitionId()
{
    return EnemyCombatDefinitionId{"enemy.infected.basic"};
}
