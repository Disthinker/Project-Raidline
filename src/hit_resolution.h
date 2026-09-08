#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include "enemy_lifecycle.h"
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
    int penetration{};
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
    std::vector<EnemyRemovalFact> removals;
    std::size_t enemiesKilled{0};
};

[[nodiscard]] std::optional<HitRegion> hitRegionAtPoint(
    const Rect &target,
    Vec2 point) noexcept;

[[nodiscard]]
HitResolutionResult resolveShotEnemyHits(
    const std::vector<ShotCollisionCandidate> &shots,
    EnemyLifecycle &enemies);

[[nodiscard]]
HitResolutionResult resolveShotHits(
    const std::vector<ShotCollisionCandidate> &shots,
    EnemyLifecycle &enemies,
    const std::vector<BallisticBlocker> &blockers);
