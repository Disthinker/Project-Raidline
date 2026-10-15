#pragma once
#include "profile_state.h"
#include "raid_lifecycle.h"
#include "raid_space_query.h"
#include "self_recovery_domain.h"

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

// Asset-contract fixtures use a clear test room and an explicit current proof.
// Production callers never use these helpers. Real frozen walls and directional
// eligibility are covered separately by the observation/service/UI regressions.
inline RaidVisibilityProjection
observeAssetContractPoint(const ProfileState &profile,
                          const RaidSpaceDefinitionId &space, Vec2 point)
{
    Vec2 size{25600, 14400};
    if (profile.pendingRaid)
        for (const auto &room : profile.pendingRaid->interiors)
            if (room.id == space)
                size = room.worldSize;
    RaidVisibilityProjection view;
    view.spaceId = space;
    view.observer = point;
    view.worldSize = size;
    view.direction = {1, 0};
    view.angleDegrees = 180;
    view.maximumDistance = 1000;
    view.environment = RaidLightEnvironment::DayOutdoor;
    view.geometryRevision = 1;
    view.valid = true;
    const float right = std::min(size.x, point.x + 500),
                top = std::max(0.0F, point.y - 500),
                bottom = std::min(size.y, point.y + 500);
    view.boundary = {{point.x, top},
                     {right, top},
                     {right, bottom},
                     {point.x, bottom},
                     point};
    return view;
}
inline InventoryReceipt pickupObservedRaidLoot(ProfileState &profile,
                                               const ContentRegistry &content,
                                               AssetInstanceId id,
                                               const CommandContext &context)
{
    RaidVisibilityProjection view;
    if (profile.pendingRaid)
        for (const auto &loot : profile.pendingRaid->loot)
            if (loot.assetId == id)
            {
                view = observeAssetContractPoint(profile, loot.spaceId,
                                                 loot.position);
                break;
            }
    const auto key = view.key();
    return pickupRaidLoot(profile, content, id, context, &view, &key);
}
inline InventoryReceipt
openObservedRaidSelfRecovery(ProfileState &profile,
                             const ContentRegistry &content,
                             const CommandContext &context)
{
    const auto point = profile.pendingRaid && profile.pendingRaid->selfRecovery
                           ? profile.pendingRaid->selfRecovery->cachePosition
                           : Vec2{};
    const auto view =
        observeAssetContractPoint(profile, outdoorRaidSpaceId(), point);
    const auto key = view.key();
    return executeOpenRaidSelfRecovery(profile, content, context, &view, &key);
}
