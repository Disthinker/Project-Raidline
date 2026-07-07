#pragma once

#include <vector>
#include "enemy.h"
#include "projectile.h"
#include "vec2.h"

struct HitResolutionResult
{
    std::vector<Vec2> hitPositions;
};

HitResolutionResult resolveProjectileEnemyHits(std::vector<Projectile> &projectiles_, std::vector<Enemy> &enemies_);