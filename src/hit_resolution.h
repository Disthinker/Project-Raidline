#pragma once

#include <cstddef>
#include <vector>

#include "enemy.h"
#include "projectile.h"
#include "vec2.h"

struct HitResolutionResult
{
    std::vector<Vec2> hitPositions;
    std::size_t enemiesKilled{0};
};

HitResolutionResult resolveProjectileEnemyHits(std::vector<Projectile> &projectiles_, std::vector<Enemy> &enemies_);