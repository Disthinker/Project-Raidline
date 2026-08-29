#pragma once

#include <optional>
#include <span>
#include <cstdint>
#include <vector>

#include "hit_resolution.h"
#include "raid_space_spatial_index.h"
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

[[nodiscard]] bool raidSpaceSegmentIntersectsBlockerInterior(
    Vec2 start,
    Vec2 end,
    Rect blocker) noexcept;

// Immutable navigation geometry for one actor footprint in one Raid space.
// Static blocker expansion and corner-to-corner visibility are built once and
// then shared by every enemy query in that space. Dynamic starts and goals are
// still resolved per query, so no actor position or target state is cached in
// the field.
class RaidSpaceNavigationField
{
public:
    [[nodiscard]] static std::optional<RaidSpaceNavigationField> build(
        Vec2 actorSize,
        Vec2 worldSize,
        std::span<const BallisticBlocker> blockers,
        float clearance = 2.0F);

    [[nodiscard]] std::optional<Vec2> nextWaypoint(
        Vec2 start,
        Vec2 goal,
        float goalTolerance) const;

    [[nodiscard]] Vec2 actorSize() const noexcept;
    [[nodiscard]] Vec2 worldSize() const noexcept;

private:
    Vec2 actorSize_{};
    Vec2 worldSize_{};
    float clearance_{};
    std::vector<Rect> expandedBlockers_;
    std::optional<RaidSpaceBlockerIndex> expandedBlockerIndex_;
    std::vector<Vec2> cornerNodes_;
    std::vector<float> cornerEdges_;
    bool usesDenseGrid_{};
    float denseGridCellSize_{};
    Vec2 denseGridOrigin_{};
    std::size_t denseGridColumns_{};
    std::size_t denseGridRows_{};
    std::vector<std::uint8_t> denseGridWalkable_;
    std::vector<std::uint8_t> denseGridConnections_;
    std::size_t portalChunkSizeCells_{};
    std::size_t portalChunkColumns_{};
    std::size_t portalChunkRows_{};
    std::vector<std::uint8_t> portalChunkConnections_;

    [[nodiscard]] std::optional<Vec2> nextDenseGridWaypoint(
        Vec2 start,
        Vec2 goal,
        float goalTolerance) const;
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
