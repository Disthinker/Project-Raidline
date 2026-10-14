#pragma once

#include "definition_id.h"
#include "rect.h"
#include <cstdint>
#include <vector>

inline constexpr float kRaidSightGeometryEpsilon{0.001F};

// Owned, transient observation data. No actor/asset pointers or save identity.
// The simulation builds it from its current space; clients never grant access.
struct RaidVisibilityProjection
{
    RaidSpaceDefinitionId spaceId;
    Vec2 observer{};
    Vec2 worldSize{};
    std::uint64_t geometryRevision{};
    std::vector<Vec2> boundary;
    bool valid{};

    [[nodiscard]] bool validGeometry() const noexcept;

    [[nodiscard]] bool matches(const RaidSpaceDefinitionId &,
                               Vec2) const noexcept;
    [[nodiscard]] bool pointVisible(Vec2) const noexcept;
    [[nodiscard]] bool boundsVisible(Rect) const;
};

[[nodiscard]] bool raidSightSegmentIntersectsRectInterior(Vec2, Vec2,
                                                          Rect) noexcept;
// Clip one fan triangle to an object rectangle; used by observation queries.
[[nodiscard]] std::vector<Vec2> clipRaidVisibilityTriangle(Vec2, Vec2, Vec2,
                                                           Rect);
