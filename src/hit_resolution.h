#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include "enemy.h"
#include "shot_resolution.h"
#include "vec2.h"

// A travelled logical segment supplied by the non-entity ballistic runtime.
struct ShotCollisionCandidate
{
    ShotId shotId{kInvalidShotId};
    Vec2 start{};
    Vec2 end{};
    float collisionExtent{};
    int damage{};
    std::optional<ShotAimIntent> aimIntent;
};

using BallisticBlockerId = std::uint64_t;

struct BallisticBlocker
{
    BallisticBlockerId id{};
    Rect bounds{};
};

struct HitResolutionResult
{
    std::vector<HitResult> hits;
    std::vector<ShotId> consumedShotIds;
    // Indices refer to the enemy vector as it existed when resolution began.
    // GameplayWorld uses them to remove parallel per-enemy runtime state after
    // resolveShotHits has removed the killed Enemy objects.
    std::vector<std::size_t> removedEnemyIndices;
    std::size_t enemiesKilled{0};
};

[[nodiscard]] std::optional<HitRegion> hitRegionAtPoint(
    const Rect &target,
    Vec2 point) noexcept;

[[nodiscard]]
HitResolutionResult resolveShotEnemyHits(
    const std::vector<ShotCollisionCandidate> &shots,
    std::vector<Enemy> &enemies);

[[nodiscard]]
HitResolutionResult resolveShotHits(
    const std::vector<ShotCollisionCandidate> &shots,
    std::vector<Enemy> &enemies,
    const std::vector<BallisticBlocker> &blockers);
