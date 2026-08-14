#pragma once

#include <cstddef>
#include <vector>

#include "enemy.h"
#include "rect.h"
#include "shot_resolution.h"
#include "vec2.h"

// A short-lived collision observation supplied by either the V0 Projectile
// adapter or the future non-entity logical flight implementation.
struct ShotCollisionCandidate
{
    ShotId shotId{kInvalidShotId};
    Rect bounds{};
    int damage{};
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
