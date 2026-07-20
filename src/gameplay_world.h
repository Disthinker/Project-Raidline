#pragma once

#include <vector>

#include "enemy.h"
#include "gameplay_input.h"
#include "particle_system.h"
#include "player.h"
#include "projectile.h"

class GameplayWorld
{
public:
    GameplayWorld();

    // 为 GameplayWorldTest 提供最小配置入口。
    // 正常游戏仍使用默认的 3 HP Enemy。
    explicit GameplayWorld(int enemyMaxHealth);

    void update(
        const GameplayInput &input,
        float deltaTime);

    const Player &player() const;
    const std::vector<Projectile> &projectiles() const;
    const std::vector<Enemy> &enemies() const;
    const std::vector<Particle> &particles() const;

    int score() const noexcept;

private:
    Player player_{640.0f, 360.0f};
    std::vector<Projectile> projectiles_;
    std::vector<Enemy> enemies_;

    float fireCooldown_{0.25f};
    float cooldownRemaining_{0.0f};

    ParticleSystem particleSystem_;
    int score_{0};
};