#pragma once
#include "profile_state.h"
#include "raid_space_query.h"

// Existing-domain test callers now provide the same frozen-space geometry as
// the service adapter; this grants no production bypass or persistent state.
inline RaidVisibilityProjection
observeFrozenRaidInterior(const ProfileState &profile,
                          const RaidSpaceDefinitionId &space, Vec2 center)
{
    if (!profile.pendingRaid)
        return {};
    for (const auto &interior : profile.pendingRaid->interiors)
        if (interior.id == space)
        {
            std::vector<BallisticBlocker> walls;
            for (const auto &r : interior.ballisticBlockers)
                walls.push_back({static_cast<std::uint64_t>(walls.size() + 1),
                                 {r.position, r.size}});
            const auto index =
                RaidSpaceBlockerIndex::build(interior.worldSize, walls);
            return index ? buildRaidSpaceVisibility(*index, space, center)
                         : RaidVisibilityProjection{};
        }
    return {};
}
