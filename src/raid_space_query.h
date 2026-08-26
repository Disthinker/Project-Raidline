#pragma once

#include <optional>
#include <span>

#include "hit_resolution.h"
#include "vec2.h"

struct RaidSpaceNavigationQuery
{
    Vec2 start{};
    Vec2 goal{};
    Vec2 actorSize{};
    Vec2 worldSize{};
    std::span<const BallisticBlocker> blockers;
    float clearance{2.0F};
    float goalTolerance{};
};

[[nodiscard]] bool raidSpaceHasLineOfSight(
    Vec2 start,
    Vec2 end,
    std::span<const BallisticBlocker> blockers) noexcept;

// Returns the next actor-center waypoint on a deterministic shortest
// visibility path. goalTolerance permits a pursuer to stop at a legal actor-
// center approach point near a semantic target that is itself too close to
// cover. A clear direct route returns goal. Invalid or unreachable input
// returns nullopt so the caller can fail closed instead of pushing into cover.
[[nodiscard]] std::optional<Vec2> nextRaidSpaceWaypoint(
    const RaidSpaceNavigationQuery &query);
