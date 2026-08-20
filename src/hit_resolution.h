#pragma once

#include <cstddef>
#include <cstdint>
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
    std::size_t enemiesKilled{0};
};

[[nodiscard]]
HitResolutionResult resolveShotEnemyHits(
    const std::vector<ShotCollisionCandidate> &shots,
    std::vector<Enemy> &enemies);

[[nodiscard]]
HitResolutionResult resolveShotHits(
    const std::vector<ShotCollisionCandidate> &shots,
    std::vector<Enemy> &enemies,
    const std::vector<BallisticBlocker> &blockers);
