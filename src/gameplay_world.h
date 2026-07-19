#pragma once

#include <vector>
#include "enemy.h"
#include "gameplay_input.h"
#include "player.h"
#include "projectile.h"
#include "hit_effect.h"
#include "particle_system.h"

class GameplayWorld
{
public:
    GameplayWorld();

    void update(const GameplayInput &input, float deltaTime);

    const Player &player() const;
    const std::vector<Projectile> &projectiles() const;
    const std::vector<Enemy> &enemies() const;
    const std::vector<HitEffect> &hitEffects() const;
    const std::vector<Particle> &particles() const;

private:
    Player player_{640.0f, 360.0f};
    std::vector<Projectile> projectiles_;
    std::vector<Enemy> enemies_;
    std::vector<HitEffect> hitEffects_;
    float fireCooldown_{0.25f};
    float cooldownRemaining_{0.0f};
    ParticleSystem particleSystem_;
};