#pragma once

#include "combat_target.h"
#include "item_definition.h"
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

// Value-only frozen facts shared by simulation and persistence. No geometry
// index, actor/container pointers, UI state or activity policy lives here.
struct GrenadeLanding
{
    Vec2 origin{}, point{};
    RaidSpaceDefinitionId space;
    GrenadeUseDefinition use;
    std::string effectIdentity;
};
struct GrenadeFlight { GrenadeLanding landing; double elapsed{}; };
struct GrenadeFuse { GrenadeLanding landing; double elapsed{}; };
struct GrenadeExplosion { std::uint64_t id{}; GrenadeLanding landing; };
struct GrenadeRuntimeCheckpoint
{
    std::optional<GrenadeFlight> flight;
    std::optional<GrenadeFuse> fuse;
    std::optional<GrenadeExplosion> explosion;
    std::uint64_t nextExplosionId{1};
};

struct NoiseDecoyLanding
{
    Vec2 origin{}, point{};
    RaidSpaceDefinitionId space;
    NoiseDecoyUseDefinition use;
    std::string effectIdentity;
};
struct NoiseDecoySource
{
    std::uint64_t id{};
    NoiseDecoyLanding landing;
    double elapsed{};
    unsigned pulses{1};
};
struct NoiseDecoyFlight { NoiseDecoyLanding landing; double elapsed{}; };
struct NoiseInvestigation
{
    RaidSpaceDefinitionId space;
    std::uint64_t source{};
    Vec2 point{}, home{};
    bool returning{};
};
struct NoiseInvestigationCheckpoint
{
    CombatTargetId targetId{};
    NoiseInvestigation investigation;
};
struct NoiseDecoyRuntimeCheckpoint
{
    std::optional<NoiseDecoyFlight> flight;
    std::optional<NoiseDecoySource> source;
    std::uint64_t nextSourceId{1};
    std::vector<NoiseInvestigationCheckpoint> investigations;
};
