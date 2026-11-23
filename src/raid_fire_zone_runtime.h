#pragma once

#include "grenade_explosion_plan.h"
#include "raid_timed_field_runtime.h"

using FireZoneLanding = RaidFieldLanding<FireZoneUseDefinition>;
using FireZoneFlight = RaidFieldFlight<FireZoneUseDefinition>;
using RaidFireZone = RaidFieldZone<FireZoneUseDefinition>;
using RaidFireZoneRuntime = RaidTimedFieldRuntime<FireZoneUseDefinition>;

struct RaidFireDamageTick
{
    std::uint64_t sequence{};
    RaidSpaceDefinitionId space;
    std::vector<GrenadeEnemyDamage> enemies;
    std::optional<CombatDamageCommand> playerDamage;
    Vec2 playerPosition{};
    int playerHealth{};
};
