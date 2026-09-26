#pragma once

#include <span>
#include <string_view>
#include "content_registry.h"
#include "raid_map_generation.h"

// Signage is immutable presentation, never a target, room boundary or map reveal.
inline std::span<const RaidRoomLabelDefinition> raidRoomSigns(
    const MapDefinition &map, const RaidSpaceDefinitionId &activeSpace, Vec2 frozenSize)
{
    for (const auto &interior : map.interiors)
        if (interior.id == activeSpace && interior.worldSize.x == frozenSize.x &&
            interior.worldSize.y == frozenSize.y)
            return interior.roomLabels;
    return {};
}

inline bool raidSignVisible(ContentRect sign, ContentRect view) noexcept
{
    return sign.position.x < view.position.x + view.size.x &&
        sign.position.y < view.position.y + view.size.y &&
        sign.position.x + sign.size.x > view.position.x &&
        sign.position.y + sign.size.y > view.position.y;
}

inline std::string_view raidLandmarkStructureSign(
    const RaidOutdoorPropSnapshot &prop,
    std::span<const RaidLandmarkPlacementSnapshot> landmarks) noexcept
{
    for (const auto &landmark : landmarks)
        for (const auto &structure : landmark.structures)
            if (structure == prop.bounds) return landmark.displayName;
    return {};
}
